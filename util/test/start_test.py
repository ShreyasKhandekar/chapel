#!/usr/bin/env python3
# PYTHON_ARGCOMPLETE_OK
#
# CHAPEL TESTING

from __future__ import print_function

import atexit
import contextlib
import fnmatch
import getpass
import glob
import logging
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile
import time

# need to be able to find the util and chplenv dir even when start_test
# doesn't live in $CHPL_HOME/util (such as for the release tarball)
util_dir = os.path.join(os.path.dirname(__file__), '..', '..', 'util')
chplenv_dir = os.path.join(util_dir, 'chplenv')
config_dir = os.path.join(util_dir, 'config')
sys.path.insert(0, os.path.abspath(util_dir))
sys.path.insert(0, os.path.abspath(chplenv_dir))
sys.path.insert(0, os.path.abspath(config_dir))

# update PATH to remove CHPL_HOME paths if any
# This essentially deactivates the venv that sub_test is run in,
# because the test venv is built inside of CHPL_HOME.
# We do this so tests/prediffs can use the system python instead of the venv
import fixpath
fixpath.update_path_env()

from chplenv import *

# these are in the test/ directory with us
import py3_compat
import re2_supports_valgrind
import check_perf_graphs

import argparse
try:
    import argcomplete
except ImportError:
    argcomplete = None

# PROGRAM ENTRY POINT
def main():
    check_environment()

    # set up the parser and parse command-line arguments
    parser = parser_setup()
    if argcomplete:
        argcomplete.autocomplete(parser)
    global args
    preprocess_options()
    args = parser.parse_args()

    check_environment_with_args()

    invocation_dir = os.getcwd()

    run_tests(args.tests)

    # in case we've changed directories
    os.chdir(invocation_dir)

    finish()


def run_tests(tests):
    set_up_tmpdir()
    set_up_logger()

    files = []
    dirs = []
    had_invalid_file = False
    for i in tests:
        if os.path.isdir(i): # a directory (also get absolute path)
            dirs.append(os.path.abspath(i))
        elif os.path.isfile(i): # a file
            files.append(os.path.abspath(i))
        else:
            logger.write("[Error: {0} is not a valid file or directory]"
                    .format(i))
            had_invalid_file = True

    if had_invalid_file and len(files) == 0 and len(dirs) == 0:
        sys.exit(2)

    # set up
    set_up_environment()
    set_up_general()
    set_up_performance_testing_A() # A and B are separate in order to keep
                                   # output the same from old start_test
    set_up_executables()
    set_up_performance_testing_B()

    # autogenerate tests from spec if no tests were given
    if len(files) == 0 and len(dirs) == 0: # no tests specified
        auto_generate_tests()
        if os.getcwd() == home:
            dirs = [test_dir]
        else:
            dirs = [os.getcwd()]

    # print out flags
    logger.write('[tests: "{0}"]'.format(" ".join(files)))
    if args.recurse:
        logger.write('[directories: "{0}"]'.format(" ".join(dirs)))
    else:
        logger.write('[directories: (nonrecursive): "{0}"]'
                .format(" ".join(dirs)))

    # check for duplicate .graph and .dat files
    check_perf_graphs.check_for_duplicates(logger, finish, test_dir, args.perflabel, args.performance, args.gen_graphs or args.comp_performance)

    # print out Chapel environment
    print_chapel_environment()

    # when the user specifies specific files, run them, even if they are
    # futures or notests
    os.environ["CHPL_TEST_FUTURES"] = "1"
    os.environ["CHPL_TEST_NOTESTS"] = "1"
    # if the user throws --respect-skipifs, skipifs will be respected for single tests
    if args.respect_skipifs:
        os.environ["CHPL_TEST_SINGLES"] = "0"
    else:
        os.environ["CHPL_TEST_SINGLES"] = "1"

    if args.respect_notests:
        os.environ["CHPL_TEST_NOTESTS"] = "0"
    else:
        os.environ["CHPL_TEST_NOTESTS"] = "1"

    for test in files:
        test_file(test)

    os.environ["CHPL_TEST_FUTURES"] = str(args.futures_mode)
    os.environ["CHPL_TEST_NOTESTS"] = "0"
    os.environ["CHPL_TEST_SINGLES"] = "0"

    # set up multiple passes/runs through the directories
    if args.performance:
        testruns = ["performance", "graph"]
    else:
        if args.gen_graphs:
            testruns = ["graph"]
        else:
            testruns = ["run"]

    for tests in dirs:
        for t in testruns:
            test_directory(tests, t)

    # test and graph compiler performance
    if args.comp_performance:
        compiler_performance()

    # generate GRAPHFILES graphs
    if args.gen_graphs:
        generate_graph_files_graphs()


# ESCAPE ROUTINES AND CLEAN-UP

def finish():
    # summarize
    if not args.clean_only:
        summarize()
    else:
        logger.write("[Summary: CLEAN ONLY]")

    logger.write()
    logger.stop()

    # copy log from per-pid location (if needed) and remove tmp dir
    cleanup()

    # produce jUnit XML test output if needed
    if args.junit_xml:
        print("[Generating jUnit XML report]")
        jUnit()

    # exit, returning 0 if no failures and 2 if there were some
    if args.clean_only or failures == 0:
        sys.exit(0)
    else:
        sys.exit(2)


# MAIN ROUTINES AND TESTING

def test_file(test):
    path_to_test = os.path.relpath(test)
    test_name = os.path.basename(test)

    with cd(os.path.dirname(test)): # cd into dir, and cd out later
        # clean executables, etc
        logger.write()
        logger.write("[Cleaning file {0}]".format(test))
        # clean and run test
        clean(test_name)

        error = 0
        if not args.clean_only:
            if args.performance or not args.gen_graphs:
                error = run_sub_test(test)

            # check for errors:
            if error != 0:
                logger.write("[Error running sub_test (code {1}) for {0}]"
                        .format(path_to_test, error))

            if args.progress:
                sys.stderr.write("[done]\n")

            del os.environ["CHPL_ONETEST"]

            if args.gen_graphs:
                generate_graphs(test)


def test_directory(test, test_type):
    logger.write("[Working from directory {0}]".format(test))

    # recurse through directory
    for root, dirs, files in os.walk(test):
        if not os.access(root, os.X_OK):
            logger.write("[Warning: Cannot cd into {0} skipping directory]"
                    .format(root))
            continue
        else:
            dir = os.path.abspath(root)

        # if the directory name ends with .dSYM, skip it
        # if a parent directory is a .dSYM, skip it
        if dir.endswith(".dSYM") or any(
            e.endswith(".dSYM") for e in os.path.normpath(dir).split(os.sep)
        ):
            continue

        logger.write()
        logger.write("[Working on directory {0}]".format(root))

        # stop recursing if flag is set
        if not args.recurse:
            del dirs[:]
        else:
            dirs.sort()

        # ignore skipifs for --clean-only run
        if not args.clean_only:
            with cd(dir): # cd into dir, and cd out later
                # SKIP IF IMPLEMENTATIONS
                test_env = os.path.join(util_dir, "test", "testEnv")

                # The below cases are ordered to check for
                # recursive skipping first, and then to check
                # for skipping only within a directory.
                # That way, if both are specified, we do
                # not visit child directories.

                # Skip this directory and child directories
                # if there is a <dir>.notest file
                notest_file_name = os.path.join(root, "..",
                        "{0}.notest".format(os.path.basename(root)))
                notest_file_name = os.path.normpath(notest_file_name)
                if os.path.isfile(notest_file_name):
                    del dirs[:]
                    continue

                # Skip this directory and child directories
                # if there is a <dir>.skipif file returning true
                prune_if = False
                skip_file_name = os.path.join(root, "..",
                        "{0}.skipif".format(os.path.basename(root)))
                skip_file_name = os.path.normpath(skip_file_name)
                if os.path.isfile(skip_file_name):
                    try:
                        prune_if = process_skipif_output(run_command([test_env, skip_file_name]).strip())
                        # check output and skip if true
                        if prune_if == "1" or prune_if == "True":
                            logger.write("[Skipping directory and children bas"
                                    "ed on .skipif environment settings in {0}]"
                                    .format(skip_file_name))
                            del dirs[:]
                            continue
                    except:
                        logger.write("[Warning: .skipif error.]")

                # Skip the directory if there is a SKIPIF file that
                # evaluates true
                skip_test = False
                if os.path.isfile("SKIPIF"):
                    try:
                        skip_test = process_skipif_output(run_command([test_env, "SKIPIF"]).strip())
                        # check output and skip if true
                        if skip_test == "1" or skip_test == "True":
                            logger.write("[Skipping directory based on SKIPIF "
                                    "environment settings]")
                            continue
                    except:
                        logger.write("[Warning: SKIPIF error.]")

                # skip this directory if there is a NOTEST file
                if os.path.isfile(os.path.join(dir, "NOTEST")):
                    continue

        # run tests in directory
        # don't run if only doing performance graphs
        if not test_type == "graph":
            # check for .chpl, .test.c(pp), or .ml-test.c(pp) files
            # and for NOTEST
            are_tests = False
            for f in files:
                if not test_type == "performance":
                    if f.endswith((".chpl", ".test.c", ".ml-test.c",
                                   ".test.cpp", ".ml-test.cpp")) :
                        are_tests = True
                        break
                    # this directory may generate test files
                    if f == "PRETEST":
                        are_tests = True
                        break
                else:
                    if f.endswith("." + perf_keys):
                        are_tests = True
                        break

            # don't run local 'sub_test's on --performance or --gen-graphs runs
            run_local_sub_test = False
            if test_type == "run":
                run_local_sub_test = os.access(os.path.join(dir, "sub_test"), os.X_OK)

            # check a lot of stuff before continuing
            if are_tests or run_local_sub_test:
                # cd to dir for clean and run, saving current location
                with cd(dir):
                    # clean dir
                    clean()

                    if not args.clean_only:
                        # run all tests in dir
                        error = run_sub_test()
                        # check for errors:
                        if not error == 0:
                            logger.write("[Error running sub_test (code {1}) in {0}]"
                                    .format(root, error))

            # let user know no tests were found
            else:
                logger.write("[No tests in directory {0}]".format(root))
        # generate graphs
        else:
            with cd(dir):
                # generate graphs for all testsin dir
                generate_graphs()


def summarize():
    date_str = time.strftime("%y%m%d.%H%M%S")
    logger.write("[Done with tests - {0}]".format(date_str))
    logger.write("[Log file: {0} ]".format(os.path.abspath(log_file)))
    logger.write()
    logger.stop()
    # setup regular expressions for searching
    future_marker = r"^Future"
    suppress_marker = r"^Suppress"
    error_marker = r"^\[Error"
    if not args.performance and args.gen_graphs:
        success_marker = r"\[Success generating"
    else:
        if args.comp_only:
            success_marker = r"\[Success compiling"
        else:
            success_marker = r"\[Success matching"
    warning_marker = r"^\[Warning"
    passing_suppressions_marker = ("{0}.*{1}"
            .format(suppress_marker, success_marker))
    passing_futures_marker = "{0}.*{1}".format(future_marker, success_marker)
    success_marker = "^" + success_marker
    skip_stdin_redirect_marker = r"^\[Skipping test with .stdin input"

    # setup counts and blank strings to hold summaries
    global failures # for exit codes later
    failures = 0
    successes = 0
    futures = 0
    warnings = 0
    passing_suppressions = 0
    passing_futures = 0
    skip_stdin_redirects = 0
    failure_summary = ""
    suppression_summary = ""
    future_summary = ""
    warning_summary = ""
    summary = "[Test Summary - {0}]\n".format(date_str)

    # scan line-by-line, logging failures, warnings, etc.. and updating counts
    with open(tmp_log_file, "r") as log:
        for line in log:
            if re.search(error_marker, line, flags=re.M):
                failure_summary += line
                failures += 1
            elif re.search(suppress_marker, line, flags=re.M):
                suppression_summary += line
            elif re.search(future_marker, line, flags=re.M):
                future_summary += line
                futures += 1
            elif re.search(warning_marker, line, flags=re.M):
                warning_summary += line
                warnings += 1
            elif re.search(success_marker, line, flags=re.M):
                successes += 1
            if re.search(passing_suppressions_marker, line, flags=re.M):
                passing_suppressions += 1
            elif re.search(passing_futures_marker, line, flags=re.M):
                passing_futures += 1
            elif re.search(skip_stdin_redirect_marker, line, flags=re.M):
                skip_stdin_redirects += 1

    # compile summary
    summary += failure_summary
    summary += suppression_summary
    summary += future_summary
    summary += warning_summary

    if skip_stdin_redirects > 0:
        logger.write("[Skipped {0} tests with .stdin input]"
                .format(skip_stdin_redirects))

    summary += ("[Summary: #Successes = {0} | #Failures = {1} | #Futures = {2} "
            "| #Warnings = {3} ]\n"
            .format(successes, failures, futures, warnings))
    summary += ("[Summary: #Passing Suppressions = {0} | "
            "#Passing Futures = {1} ]\n"
            .format(passing_suppressions, passing_futures))
    summary += "[END]\n"

    # log summary, and write it to its own .summary file
    logger.restart()
    logger.write(summary)

    with open(tmp_summary_file, "w") as log_summary:
        log_summary.write(summary)

    # Note: Log file & summary copied tmp_log_file to log_file in cleanup

def clean(test=False):
    date_str = time.strftime("%a %b %d %H:%M:%S %Z %Y")

    # clean executables, tmps, etc.
    sub_clean = os.path.join(util_dir, "test", "sub_clean")
    try:
        if test: # single test
            logger.write("[Starting {0} {1} {2}]"
                    .format(sub_clean, test, date_str))
            out = run_command([sub_clean, test])
        else:
            out = run_command([sub_clean])
        logger.write(out)
    except:
        logger.write("[Error: sub_clean error]")


def run_sub_test(test=False):
    date_str = time.strftime("%a %b %d %H:%M:%S %Z %Y")
    os.environ["CHPL_TEST_UTIL_DIR"] = util_dir

    # run test
    logger.write()
    if test: # single test
        logger.write("[Working on file {0}]".format(os.path.relpath(test)))
        os.environ["CHPL_ONETEST"] = os.path.basename(test)

    if os.access("sub_test", os.X_OK):
        sub_test = os.path.abspath("sub_test")
    else:
        sub_test = os.path.join(util_dir, "test", "sub_test")

    if args.progress and test:
        sys.stderr.write("Testing {0} ... \n".format(test))

    logger.write("[Starting {0} {1}]".format(sub_test, date_str))
    status = run_and_log([sub_test, compiler])
    return status


def generate_graphs(test=False):
    if test:
        basedir = os.path.dirname(test)
        remove_dot = test.replace(".chpl", "")
        remove_dot = remove_dot.replace(".ml-test.cpp", "")
        remove_dot = remove_dot.replace(".ml-test.c", "")
        remove_dot = remove_dot.replace(".test.cpp", "")
        remove_dot = remove_dot.replace(".test.c", "")
        graph_files = [(remove_dot + ".graph")]
        # exit if it isn't actually a file
        if not os.path.isfile(graph_files[0]):
            return
    else:
        basedir = os.getcwd()
        graph_files = glob.glob("*.graph")
        # exit if no files matched
        if len(graph_files) == 0 or os.environ.get("CHPL_TEST_PERF_DIR"):
            return

    logger.write("[Executing genGraphs for graph_files in {0} in {1}]"
            .format(basedir, perf_html_dir))

    cmd = [create_graphs]
    cmd += args.gen_graph_opts.split(" ")
    cmd += ["-p", perf_dir, "-o", perf_html_dir, "-n", perf_test_name]
    cmd += start_date_t.split(" ")
    cmd += [args.graphs_disp_range, "-r", args.graphs_gen_default]
    cmd += graph_files
    status = run_and_log(cmd)

    if status == 0:
        logger.write("[Success generating graphs for graph_files in {0} in {1}]"
                .format(basedir, perf_html_dir))
    else:
        logger.write("[Error generating graphs for graph_files in {0} in {1}]"
                .format(basedir, perf_html_dir))


def compiler_performance():
    end_time = int(time.time())
    elapsed = end_time - start_time

    comp_graph_list = os.path.join(test_dir, "COMPGRAPHFILES")

    # combine smaller .dat files
    try:
        logger.write("[Combining dat files now]")
        out = run_command([combine_comp_perf, "--tempDatDir",
            temp_dat_dir,"--elapsedTestTime", str(elapsed), "--outDir",
            comp_perf_dir])
        logger.write(out)
        logger.write("[Success combining compiler performance dat files]")
    except:
        logger.write("[Error combining compiler performance dat files]")

    # create the graphs
    title = "Chapel Compiler Performance Graphs"
    logger.write("[Creating compiler performance graphs now]")

    cmd = [create_graphs]
    cmd += args.gen_graph_opts.split(" ")
    cmd += ["-p", comp_perf_dir, "-o", comp_perf_html_dir, "-a", title, "-n",
            compperf_test_name]
    cmd += start_date_t.split(" ")
    cmd += ["-g", comp_graph_list, "-t", test_dir]
    cmd += "-m all:v,examples:v -x".split(" ")
    status = run_and_log(cmd)
    if status == 0:
        logger.write("[Success generating compiler performance graphs from "
                "{0} in {1}]".format(comp_graph_list, comp_perf_html_dir))
    else:
        logger.write("[Error generating compiler performance graphs from "
            "{0} in {1}]".format(comp_graph_list, comp_perf_html_dir))

    # delete temp files
    shutil.rmtree(temp_dat_dir)


def generate_graph_files_graphs():
    graphfiles_prefix = ''
    if args.perflabel != "perf":
        graphfiles_prefix = args.perflabel.upper()

    exec_graph_list = os.path.join(test_dir, "{0}GRAPHFILES".format(graphfiles_prefix))

    logger.write("[Executing genGraphs for {0} in {1}]"
            .format(exec_graph_list, perf_html_dir))

    cmd = [create_graphs]
    cmd += args.gen_graph_opts.split(" ")
    cmd += ["-p", perf_dir, "-o", perf_html_dir, "-t", test_dir, "-n",
            perf_test_name]
    cmd += start_date_t.split(" ")
    cmd += [args.graphs_disp_range, "-r", args.graphs_gen_default, "-g",
        exec_graph_list]
    status = run_and_log(cmd)

    if status == 0:
        logger.write("[Success generating graphs from {0} in {1}]"
                .format(exec_graph_list, perf_html_dir))
    else:
        logger.write("[Error generating graphs from {0} in {1}]"
                .format(exec_graph_list, perf_html_dir))


# SET UP ROUTINES

def check_environment():
    if "CHPL_DEVELOPER" in os.environ: # unset CHPL_DEVELOPER
        del os.environ["CHPL_DEVELOPER"]

    if "CHPL_EXE_NAME" in os.environ:  # unset CHPL_EXE_NAME
        del os.environ["CHPL_EXE_NAME"]
        
    if "CHPL_UNWIND" in os.environ:    # squash CHPL_UNWIND output
        os.environ["CHPL_RT_UNWIND"] = "0"

    # Check for $CHPL_HOME
    global home
    if "CHPL_HOME" in os.environ:
        home = os.environ["CHPL_HOME"]
        if not os.path.isdir(home):
            print("Error: CHPL_HOME must be a legal directory.")
            sys.exit(1)
    else:
        print("Error: CHPL_HOME is not set.")
        sys.exit(1)

    # Allow for utility directory override.
    # Useful when running more recent testing system on an older version of
    # Chapel.
    global util_dir
    if "CHPL_TEST_UTIL_DIR" in os.environ:
        util_dir = os.environ["CHPL_TEST_UTIL_DIR"]
    else:
        util_dir = os.path.normpath(os.path.join(home, "util"))
        os.environ["CHPL_TEST_UTIL_DIR"] = util_dir
        if not os.path.isdir(util_dir):
            print("Error: Cannot find {0}.".format(util_dir))
            sys.exit(1)

def check_environment_with_args():
    # find test dir and check for access
    global test_dir, auto_gen_spec_tests

    auto_gen_spec_tests = os.access(os.path.join(home, "spec"), os.F_OK)

    if args.test_root_dir:
        test_dir = str(args.test_root_dir)
    else:
        test_dir = os.path.join(home, "test")
        if (not os.access(test_dir, os.F_OK) or not os.access(test_dir, os.W_OK)
            or not os.access(test_dir, os.X_OK)):
            test_dir = os.path.join(home, "examples")
            auto_gen_spec_tests = False
            if (not os.access(test_dir, os.F_OK) or not os.access(test_dir, os.W_OK)
                or not os.access(test_dir, os.X_OK)):
                test_dir = os.getcwd()

    if (not os.access(test_dir, os.F_OK) or not os.access(test_dir, os.W_OK)
        or not os.access(test_dir, os.X_OK)):
        print("Cannot write to test directory {0}".format(test_dir))
        sys.exit(-1)

    # find Logs directory and check for access
    global logs_dir
    logs_dir = os.path.join(test_dir, "Logs")
    if not os.path.isdir(logs_dir):
        py3_compat.makedirs(logs_dir, exist_ok=True)
    if not os.access(logs_dir, os.W_OK):
        print("Cannot write to Logs directory {0}".format(logs_dir))
        sys.exit(-1)

    # save host and target platforms, and configure environment appropriately
    global host_platform, host_bin_subdir, tgt_platform
    host_platform = chpl_platform.get("host")
    host_bin_subdir = chpl_bin_subdir.get("host")
    tgt_platform = chpl_platform.get("target")

    # Adjust the C environment to UTF-8 with C sorting order
    # This should not affect Chapel program behavior but it might
    # affect other elements of the test system (e.g. `sort` called
    # in a prediff).
    os.environ["LC_COLLATE"] = "C"
    os.environ["LANG"] = "en_US.UTF-8"
    if "LC_ALL" in os.environ:
        del os.environ["LC_ALL"]

    global log_file
    global tmp_log_file
    log_file = os.path.join(logs_dir, "{0}.{1}.log"
            .format(getpass.getuser(), tgt_platform))
    tmp_log_file = os.path.join(logs_dir, "{0}.{1}.pid{2}.log"
            .format(getpass.getuser(), tgt_platform, os.getpid()))


# Strip quotes that might have been added by preprocess_options()
def strip_preprocessing(arg):
    if arg.startswith("'") and arg.endswith("'"):
        return arg[1:-1]
    else:
        return arg

def set_up_tmpdir():
    # create temporary directory
    global chpl_test_tmp_dir
    if "CHPL_TEST_TMP_DIR" in os.environ:
        chpl_test_tmp_dir = os.environ["CHPL_TEST_TMP_DIR"]
    else:
        chpl_test_tmp_dir = tempfile.mkdtemp(prefix="chplTestTmpDir.")
        os.environ["CHPL_TEST_TMP_DIR"] = chpl_test_tmp_dir

    # register atexit handler (cleans up temporary directory)
    atexit.register(cleanup)


def set_up_environment():
    # compopts (note that we have to strip out our preprocessing)
    if args.compopts:
        args.compopts = [strip_preprocessing(x) for x in args.compopts]
        args.compopts = "--cc-warnings " + " ".join(args.compopts)
    else:
        args.compopts = "--cc-warnings"

    # execopts (note that we have to strip out our preprocessing)
    if args.execopts:
        args.execopts = [strip_preprocessing(x) for x in args.execopts]
        args.execopts = " ".join(args.execopts)
    else:
        args.execopts = ""

    # memory leak
    if args.mem_leaks:
        os.environ["CHPL_MEM_LEAK_TESTING"] = "true"
        args.execopts += " --memLeaks"

    # memory leak log
    if args.mem_leaks_log:
        os.environ["CHPL_MEM_LEAK_TESTING"] = "true"
        memleaksfile = os.path.abspath(args.mem_leaks_log)
        args.execopts += " --memLeaksLog=" + memleaksfile
        if os.path.isfile(memleaksfile):
            logger.write("[Removing memory leaks log file with duplicate name {0}]"
                         .format(memleaksfile))
            os.remove(memleaksfile)

    # performance (linking flags to each other)
    if args.performance or args.performance_description or args.perflabel != "perf":
        args.performance = True
        args.gen_graphs = True
        args.compopts = "--fast " + args.compopts

    if args.performance_configs:
        args.gen_graph_opts += " --configs " + args.performance_configs

    if args.comp_performance_description:
        args.comp_performance = True

    # compiler only
    if args.comp_only:
        os.environ["CHPL_COMPONLY"] = "true"

    # stdin redirection
    if args.no_stdin_redirect or args.stdin_redirect:
        os.environ["CHPL_NO_STDIN_REDIRECT"] = "true"
        if args.stdin_redirect:
            os.environ["CHPL_TEST_FORCE_STDIN_REDIRECT"] = "true"

    # launcher timeout
    if args.launcher_timeout:
        os.environ["CHPL_LAUNCHER_TIMEOUT"] = str(args.launcher_timeout)

    # num trials
    os.environ["CHPL_TEST_NUM_TRIALS"] = args.num_trials

    # test root dir
    if args.test_root_dir:
        os.environ["CHPL_TEST_ROOT_DIR"] = str(args.test_root_dir)

    # jUnit
    if args.junit_xml_file:
        args.junit_xml = True


def set_up_logger():
    # log_file
    global tmp_log_file
    global tmp_summary_file
    global log_file
    global summary_file

    # Compute the paths needing to be computed
    # Default log_file and tmp_log_file are set in check_environment_with_args
    if args.log_file:
        log_file = os.path.abspath(args.log_file)
        # Don't use temporary log files if the user specified -logfile
        tmp_log_file = log_file

    summary_file = log_file + ".summary"
    tmp_summary_file = tmp_log_file + ".summary"

    log_file_dir = os.path.dirname(log_file)
    if not os.access(log_file_dir, os.X_OK):
        print("[Permission denied for log_file directory: {0}]"
                .format(log_file_dir))
        sys.exit(1)

    # Remove log file and summary file
    if os.path.isfile(log_file):
        print()
        print("[Removing log file with duplicate name {0}]".format(log_file))
        os.remove(log_file)
    if os.path.isfile(summary_file):
        os.remove(summary_file)

    ## START LOGGING TO FILE ##
    global logger
    logger = Logger()


def set_up_general():
    global start_time
    if args.comp_performance:
        start_time = int(time.time())

    logger.write("[Starting Chapel regression tests - {0}]"
            .format(time.strftime("%y%m%d.%H%M%S")))

    branch = run_git_command(["rev-parse", "--abbrev-ref", "HEAD"])
    sha = run_git_command(["rev-parse", "--short", "HEAD"])
    logger.write("[git branch: '{0}' ({1})]".format(branch, sha))

    # check to see if we are in subdir of CHPL_HOME
    if args.chpl_home_warn:
        norm_cwd  = os.path.normpath(os.path.realpath(os.getcwd()))
        norm_home = os.path.normpath(os.path.realpath(home))
        if norm_cwd.find(norm_home) < 0:
            print ("[Warning: start_test not invoked from a subdirectory of "
                    "$CHPL_HOME]")

    # log some messages
    logger.write('[starting directory: "{0}"]'.format(os.getcwd()))
    logger.write('[Logs directory: "{0}"]'.format(logs_dir))
    logger.write('[log_file: "{0}"]'.format(log_file))
    logger.write("[CHPL_HOME: {0}]".format(home))
    logger.write("[host platform: {0}]".format(host_platform))
    logger.write("[target platform: {0}]".format(tgt_platform))

    # valgrind
    if args.valgrind:
        logger.write("[valgrind: ON]")
        try:
            # get first line of output
            binary = run_command(["which", "valgrind"]).split("\n")[0]
        except:
            logger.write("[Error: Could not find valgrind.]")
            finish()


        # get first line of output
        try:
            version = run_command(["valgrind", "--version"]).split("\n")[0]
        except:
            pass
        logger.write("[valgrind binary: {0}]".format(binary))
        logger.write("[valgrind version: {0}]".format(version))
        os.environ["CHPL_TEST_VGRND_COMP"] = "on"
        os.environ["CHPL_TEST_VGRND_EXE"] = "on"
    else:
        os.environ["CHPL_TEST_VGRND_COMP"] = "off"
        if args.valgrind_exe:
            logger.write("[valgrind: EXE only]")
            os.environ["CHPL_TEST_VGRND_EXE"] = "on"
        else:
            logger.write("[valgrind: OFF]")
            os.environ["CHPL_TEST_VGRND_EXE"] = "off"

    if args.valgrind or args.valgrind_exe:
        # Stay below valgrind's --max-threads option, which defaults to 500
        if not "CHPL_RT_NUM_THREADS_PER_LOCALE" in os.environ:
            os.environ["CHPL_RT_NUM_THREADS_PER_LOCALE"] = "450"
        else:
            logger.write("[Warning: Deadlock is possible since you set CHPL_RT_NUM_THREADS_PER_LOCALE]")

        # Squash the warning about the potential for deadlock when setting
        # the number of threads, or all tests will fail with that warning
        os.environ["CHPL_RT_NUM_THREADS_PER_LOCALE_QUIET"] = "yes"

        # Additionally, fail with an error if valgrind testing is running without
        # tasks=fifo, mem=cstdlib, or with re2 built w/o valgrind support
        if chpl_tasks.get() != "fifo":
            logger.write("[Error: valgrind requires tasks=fifo - try quickstart]")
            finish()
        if chpl_mem.get('target') != "cstdlib":
            logger.write("[Error: valgrind requires mem=cstdlib - try quickstart]")
            finish()
        if chpl_re2.get() != 'none' and not re2_supports_valgrind.get():
            logger.write("[Error: valgrind requires re2=none or re2 built with CHPL_RE2_VALGRIND_SUPPORT]")
            finish()


    # compiler
    global compiler
    if not args.compiler:
        compiler = os.path.expandvars("$CHPL_HOME/bin/{0}/chpl"
                .format(host_bin_subdir))
    else:
        compiler = args.compiler


def set_up_performance_testing_A():
    # performance
    if args.performance:
        logger.write("[performance tests: ON]")
        os.environ["CHPL_TEST_PERF"] = "on"
        if not args.perflabel == "perf":
            logger.write("[performance label: {0}]".format(args.perflabel))
        os.environ["CHPL_TEST_PERF_LABEL"] = args.perflabel
    else:
        logger.write("[performance tests: OFF]")

    # compiler performance
    if args.comp_performance:
        logger.write("[compiler performance tests: ON]")
        os.environ["CHPL_TEST_COMP_PERF"] = "on"
    else:
        logger.write("[compiler performance tests: OFF]")

    # this is here and not in setupexecutables for legacy compatibility reasons
    logger.write("[number of trials: {0}]"
            .format(os.environ["CHPL_TEST_NUM_TRIALS"]))

    # graphs
    if args.gen_graphs:
        logger.write("[performance graph generation: ON]")
        if args.graphs_disp_range:
            logger.write("[performance graph ranges: ON]")
            args.graphs_disp_range = ""
        else:
            logger.write("[performance graph ranges: OFF]")
            args.graphs_disp_range = "--no-bounds"
        logger.write("[performance graph data reduction: {0}]"
                .format(args.graphs_gen_default))
    else:
        logger.write("[performance graph generation: OFF]")


def set_up_performance_testing_B():
    # common to performance and graphs
    if args.performance or args.gen_graphs:
        # set global variables for later access and use
        global perf_dir, perf_html_dir, perf_test_name

        perf_test_name = os.environ.get('CHPL_TEST_PERF_CONFIG_NAME',
                                        platform.node().split(".")[0].lower())

        if not os.environ.get("CHPL_TEST_PERF_DIR"):
            perf_dir = os.path.expandvars("$CHPL_HOME/test/perfdat/"
                    + perf_test_name)
            os.environ["CHPL_TEST_PERF_DIR"] = perf_dir
            logger.write("[Warning: CHPL_TEST_PERF_DIR must be set for gener"
                    "ating performance graphs, using default {0}]"
                    .format(perf_dir))
        else:
            perf_dir = os.environ["CHPL_TEST_PERF_DIR"]

        perf_desc = args.performance_description
        perf_html_dir = os.path.join(perf_dir, perf_desc, "html")

        if (perf_desc != "" and perf_desc != "default"):
            os.environ["CHPL_TEST_PERF_DESCRIPTION"] = perf_desc

    global perf_keys
    perf_keys = "{0}keys".format(args.perflabel)

    # compiler
    if args.comp_performance:
        # set global variables
        global compperf_test_name, comp_perf_dir, temp_dat_dir, comp_perf_html_dir
        global combine_comp_perf

        compperf_test_name = os.environ.get('CHPL_TEST_PERF_CONFIG_NAME',
                                            platform.node().split(".")[0].lower())

        if "CHPL_TEST_COMP_PERF_DIR" in os.environ:
            comp_perf_dir = os.environ["CHPL_TEST_COMP_PERF_DIR"]
        else:
            comp_perf_dir = os.path.join(home, "test", "compperfdat",
                                         compperf_test_name)
            logger.write("[Warning: CHPL_COMP_TEST_PERF DIR must be set for "
                    "generating compiler performance graphs, using default {0}]"
                    .format(comp_perf_dir))

        compperf_test_name += args.comp_performance_description

        temp_dat_dir = os.path.join(chpl_test_tmp_dir, "tempCompPerfDatFiles", "")
        os.environ["CHPL_TEST_COMP_PERF_TEMP_DAT_DIR"] = temp_dat_dir
        #remove it if it wasn't cleaned up last time
        if os.path.isdir(temp_dat_dir):
            shutil.rmtree(temp_dat_dir)

        comp_perf_html_dir = os.path.join(comp_perf_dir, "html")

        combine_comp_perf = os.path.join(util_dir, "test", "combineCompPerfData")

    # for any performance testing
    if args.performance or args.comp_performance or args.gen_graphs:
        global start_date_t, create_graphs

        if args.start_date:
            start_date_t = "-s " + args.start_date
        else:
            start_date_t = ""

        create_graphs = os.path.join(util_dir, "test", "genGraphs")

        if not args.gen_graph_opts:
            args.gen_graph_opts = ""

    if args.performance or args.comp_performance:
        # SHA for current run, in order to track dates to commits
        if args.performance:
            dat_dir = os.path.join(perf_dir, perf_desc)
        else:
            dat_dir = comp_perf_dir

        if not os.path.isdir(dat_dir):
            py3_compat.makedirs(dat_dir, exist_ok=True)

        sha_pef_keys_name = os.path.join(chpl_test_tmp_dir, "sha.perf_keys")
        with open(sha_pef_keys_name, "w") as shaPerfKeys:
            shaPerfKeys.write("sha ")

        sha_out_name = os.path.join(chpl_test_tmp_dir, "sha.exec.out.tmp")
        with open(sha_out_name, "w") as sha_out:
            try:
                output = run_git_command(["rev-parse", "HEAD"])
                sha_out.write("sha " + output)
            except:
                pass

        sha_dat_file_name = "perfSha"
        sha_dat_file_path = os.path.join(dat_dir, "{0}.dat"
                .format(sha_dat_file_name))

        logger.write("[Saving current git sha to {0}]"
                .format(sha_dat_file_path))
        try:
            cmd = os.path.join(util_dir, "test", "computePerfStats")
            out = run_command([cmd, sha_dat_file_name, dat_dir,
                sha_pef_keys_name, sha_out_name])
            logger.write(out)
        except:
            logger.write("[Error: Failed to save current sha to {0}]"
                    .format(sha_dat_file_path))


def set_up_executables():
    # check for compiler
    global compiler
    if os.path.isfile(compiler) and os.access(compiler, os.X_OK):
        compiler = os.path.abspath(compiler)

        logger.write('[compiler: "{0}"]'.format(compiler))
    else:
        logger.write("[Error: Cannot find or execute compiler: {0}]"
                .format(compiler))
        finish()

    # log more options
    logger.write('[compopts: "{0}"]'.format(args.compopts))
    os.environ['COMPOPTS'] = args.compopts

    logger.write('[execopts: "{0}"]'.format(args.execopts))
    os.environ['EXECOPTS'] = args.execopts

    logger.write('[launchcmd: "{0}"]'.format(args.launch_cmd))
    os.environ['LAUNCHCMD'] = os.path.expandvars(args.launch_cmd)

    comm = chpl_comm.get()
    network_atomics = chpl_atomics.get('network')
    comm_substrate = chpl_comm_substrate.get()
    launcher = chpl_launcher.get()
    locale_model = chpl_locale_model.get()

    logger.write('[comm: "{0}"]'.format(comm))
    os.environ["CHPL_COMM"] = comm
    os.environ["CHPL_GASNET_SEGMENT"] = chpl_comm_segment.get()
    os.environ["CHPL_LAUNCHER"] = launcher

    logger.write('[networkAtomics: "{0}"]'.format(network_atomics))
    os.environ["CHPL_NETWORK_ATOMICS"] = network_atomics

    logger.write('[localeModel: "{0}"]'.format(locale_model))
    os.environ["CHPL_LOCALE_MODEL"] = locale_model

    os.environ["CHPL_LLVM"] = chpl_llvm.get()
    os.environ["CHPL_TASKS"] = chpl_tasks.get()

    # skip stdin tests for most custom launchers, except for amdprun and slurm
    if (launcher != "none" and launcher != "amudprun" and launcher !=
        "slurm-srun" and not os.environ.get("CHPL_NO_STDIN_REDIRECT")
        and not os.environ.get("CHPL_TEST_FORCE_STDIN_REDIRECT")):
        print ("[Info: assuming stdin redirection is not supported, skipping "
            "tests with stdin]")
        os.environ["CHPL_NO_STDIN_REDIRECT"] = "true"

    # launcher timeout
    if (not os.environ.get("CHPL_LAUNCHER_TIMEOUT")
        and not os.environ.get("CHPL_TEST_DONT_SET_LAUNCHER_TIMEOUT")):
        if "slurm" in launcher:
            os.environ["CHPL_LAUNCHER_TIMEOUT"] = "slurm"
        if "pbs" in launcher or "qsub" in launcher:
            os.environ["CHPL_LAUNCHER_TIMEOUT"] = "pbs"

    # comm
    if comm != "none" and args.num_locales == "0":
        args.num_locales = "1"

    if args.num_locales == "0":
        logger.write('[numlocales: "(default)"]')
    else:
        logger.write('[numlocales: "{0}"]'.format(args.num_locales))

    os.environ["NUMLOCALES"] = args.num_locales

    if args.max_locales == "0":
        logger.write('[max_locales: "(default)"]')
    else:
        logger.write('[max_locales: "{0}"]'.format(args.max_locales))
        os.environ["CHPL_TEST_MAX_LOCALES"] = args.max_locales

    # only run multilocale tests
    if args.multilocale_only:
        if comm != "none":
            os.environ["CHPL_TEST_MULTILOCALE_ONLY"] = "true"
        else:
            logger.write("[Warning: Ignoring --multilocale-only for CHPL_COMM=none]")

    # pre-exec
    if args.preexec:
        chpl_system_preexec = []
        for preexec in args.preexec:
            if os.path.isfile(preexec) and os.access(preexec, os.X_OK):
                preexec = os.path.abspath(preexec)
                logger.write("[system-wide preexec: {0}]".format(preexec))
            else:
                logger.write("[Error: Cannot find or execute system-wide preexec: "
                        "{0}".format(preexec))
                finish()
            chpl_system_preexec.append(preexec)
        os.environ["CHPL_SYSTEM_PREEXEC"] = ','.join(chpl_system_preexec)


    # pre-diff
    chpl_system_prediff = []
    if args.prediff:
        for prediff in args.prediff:
            if os.path.isfile(prediff) and os.access(prediff, os.X_OK):
                prediff = os.path.abspath(prediff)
                logger.write("[system-wide prediff: {0}]".format(prediff))
            else:
                logger.write("[Error: Cannot find or execute system-wide prediff: "
                        "{0}".format(prediff))
                finish()
            chpl_system_prediff.append(prediff)
    if "slurm" in launcher:
        # With Slurm-based launcher, auto-run prediff-for-slurm.
        prediff_for_slurm = os.path.join(util_dir, "test", "prediff-for-slurm")
        if prediff_for_slurm not in chpl_system_prediff:
            chpl_system_prediff.append(prediff_for_slurm)
    if 'lsf-' in launcher:
        # With lsf-based launcher, auto-run prediff-for-lsf.
        prediff_for_lsf = os.path.join(util_dir, "test", "prediff-for-lsf")
        if prediff_for_lsf not in chpl_system_prediff:
            chpl_system_prediff.append(prediff_for_lsf)
    if 'ucx' in comm_substrate:
        # With ucx-based launcher, auto-run prediff-for-ucx.
        prediff_for_ucx = os.path.join(util_dir, "test", "prediff-for-ucx")
        if prediff_for_ucx not in chpl_system_prediff:
            chpl_system_prediff.append(prediff_for_ucx)

    # TODO: remove this when
    # https://github.com/chapel-lang/chapel/issues/27262 is resolved
    if 'darwin' == host_platform:
        prediff_for_debug = os.path.join(util_dir, "test", "prediff-for-debug")
        if prediff_for_debug not in chpl_system_prediff:
            chpl_system_prediff.append(prediff_for_debug)

    if chpl_system_prediff:
        os.environ["CHPL_SYSTEM_PREDIFF"] = ','.join(chpl_system_prediff)


def auto_generate_tests():
    if not auto_gen_spec_tests:
        return
    path = os.getcwd()
    with cd(home):
        if path == home or path == test_dir and not args.clean_only:
            if not args.gen_graphs:
                logger.write("[Generating tests from the Chapel Spec in "
                        "{0}/spec".format(home))
                returncode = subprocess.call(["make", "spectests"])
                if returncode != 0:
                    logger.write("[Error: Failed to generate Spec tests. Run "
                            "'make spectests' in {0} for more info]"
                            .format(home))
                    finish()


def print_chapel_environment():
    logger.write()
    logger.write("### Chapel Environment ###")
    try:
        out = run_command([os.path.join(util_dir, "printchplenv"), "--all", "--no-tidy"])
        logger.write(out)
    except:
        pass
    logger.write("##########################")

# END STUFF

# Execute 'cmd' and return its exit code.
# Print its output to the console in real-time and log it too.
def run_and_log(cmd):
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    printout(p.stdout)
    p.wait()
    return p.returncode

def cleanup():
    # move the log from the temp log
    if os.path.isfile(tmp_log_file):
        if tmp_log_file != log_file:
            shutil.copyfile(tmp_log_file, log_file)
            os.remove(tmp_log_file)

    # move the summary from the temp summary
    if os.path.isfile(tmp_summary_file):
        if tmp_summary_file != summary_file:
            shutil.copyfile(tmp_summary_file, summary_file)
            os.remove(tmp_summary_file)

    if os.path.isdir(chpl_test_tmp_dir):
      shutil.rmtree(chpl_test_tmp_dir)


def jUnit():
    junit_args = ["--start-test-log={0}".format(log_file)]
    if args.junit_xml_file:
        junit_args.append("--junit-xml={0}".format(args.junit_xml_file))

    if args.junit_remove_prefix:
        junit_args.append(
                "--remove-prefix={0}".format(args.junit_remove_prefix))
    elif args.test_root_dir:
        # if --test-root was thrown, remove it from junit report
        junit_args.append("--remove-prefix={0}".format(args.test_root_dir))

    cmd = [os.path.join(util_dir, "test",
            "convert_start_test_log_to_junit_xml.py")]

    try:
        run_command(cmd + junit_args)
    except:
        print("[ERROR generating jUnit XML report]")

    return


# PARSER


def help_all(help_msg):
    if any("help-all" in s for s in sys.argv):
        return help_msg
    else:
        return argparse.SUPPRESS


def parser_setup():
    parser = argparse.ArgumentParser(description="Test Chapel code.")
    # main args
    p = parser.add_argument("tests", nargs="*", help="test files or directories")
    if argcomplete:
        p.completer = argcomplete.completers.FilesCompleter([".chpl", ".test.c", ".test.cpp", ".ml-test.c", ".ml-test.cpp"])

    # executing options
    parser.add_argument("-execopts", "--execopts", action="append",
            dest="execopts", help="set options for executing tests")
    # lame hack to prevent abbreviations from not getting preprocessed in
    # preprocess_options() (makes things like --execop ambiguous)
    parser.add_argument("-execopts ", "--execopts ", action="append",
            dest="execopts", help=argparse.SUPPRESS)
    # compiler
    parser.add_argument("-compiler", "--compiler", action="store",
            dest="compiler", help=help_all("set alternate compiler"))
    # program launch utility
    parser.add_argument("-launchcmd", "--launchcmd", action="store",
            dest="launch_cmd", default=os.getenv("CHPL_TEST_LAUNCHCMD", ""),
            help="set a program to launch generated executables")
    # compiler options
    parser.add_argument("-compopts", "--compopts", action="append",
            dest="compopts", help="set options for the compiler")
    # lame hack to prevent abbreviations from not getting preprocessed in
    # preprocess_options() (makes things like --compop ambiguous)
    parser.add_argument("-compopts ", "--compopts ", action="append",
            dest="compopts", help=argparse.SUPPRESS)
    # log_file
    parser.add_argument("-logfile", "--logfile", action="store",
            dest="log_file", help="set alternate log file")
    # mem leaks
    parser.add_argument("-memleaks", "--memleaks", "-memLeaks", "--memLeaks",
            action="store_true", dest="mem_leaks", help="run with --memLeaks")
    # mem leaks log
    parser.add_argument("-memleakslog", "--memleakslog", "-memLeaksLog",
            "--memLeaksLog", action="store", dest="mem_leaks_log", help=("set"
            " location for memLeaks log, and run with --memLeaksLog"))
    # valgrind
    parser.add_argument("-valgrind", "--valgrind", action="store_true",
            dest="valgrind", help="run everything using valgrind")
    parser.add_argument("-valgrindexe", "--valgrindexe", action="store_true",
            dest="valgrind_exe", help="execute tests using valgrind")
    # pre exec, pre- etc....
    parser.add_argument("-syspreexec", "--syspreexec", action="append",
                        dest="preexec",
                        help="set a PREEXEC script to run before execution"
                             ", may be given more than once")
    parser.add_argument("-sysprediff", "--sysprediff", action="append",
                        dest="prediff",
                        help="set a PREDIFF script to run on test output"
                             ", may be given more than once")
    # future mode args
    futures_mode = 0
    parser.add_argument("-futures", "--futures", action="store_const", const=1,
            default=futures_mode, dest="futures_mode", help="run future tests")
    parser.add_argument("-futuresonly", "--futuresonly", "-futures-only",
            "--futures-only", action="store_const", const=2, default=
            futures_mode, dest="futures_mode", help="only run future tests")
    parser.add_argument("-futuresskipif", "--futuresskipif", "-futures-skipif",
            "--futures-skipif", action="store_const", const=3, default=
            futures_mode, dest="futures_mode", help="run future-skipif tests")
    parser.add_argument("-futures-mode", "--futures-mode", action="store",
            default=futures_mode, dest="futures_mode",
            help=argparse.SUPPRESS)
    # cleaning
    parser.add_argument("-clean-only", "-cleanonly", "--clean-only",
            "--cleanonly", action="store_true", dest="clean_only",
            help="only clean files specified in CLEANFILES")
    # recurse
    parser.add_argument("-norecurse", "-no-recurse", "--norecurse",
            "--no-recurse", action="store_false", dest="recurse",
            help="don't recurse down through directories")
    # performance
    parser.add_argument("-performance", "--performance", action="store_true",
            dest="performance", help="run performance tests")
    p = parser.add_argument("-perflabel", "--perflabel", action="store",
               default="perf", dest="perflabel",
               help="set alternate performance test label")
    if argcomplete:
        p.completer = argcomplete.completers.ChoicesCompleter(('ml-', ))

    parser.add_argument("-performance-description",
            "--performance-description", action="store", default="",
            dest="performance_description", help=help_all(""))
    parser.add_argument("-performance-configs", "--performance-configs",
            "-performance-configurations", "--performance-configurations",
            action="store", dest="performance_configs",
            help=help_all("set performance configurations"))
    parser.add_argument("-compperformance", "--compperformance",
            action="store_true", dest="comp_performance",
            help=help_all("test compiler performance"))
    parser.add_argument("-compperformance-description",
            "--compperformance-description", action="store", default="",
            dest="comp_performance_description", help=help_all(""))
    parser.add_argument("-numtrials", "--numtrials", "-num-trials",
        "--num-trials", action="store", dest="num_trials",
        default=os.getenv("CHPL_TEST_NUM_TRIALS", "1"),
        help="the number of times to run the performance tests")
    # graphing
    parser.add_argument("-gen-graphs", "--gen-graphs", "-generate-graphs",
            "--generate-graphs", action="store_true", dest="gen_graphs",
            help=help_all("only generate graphs, don't run tests"))
    parser.add_argument("-nodisplaygraphrange", "--nodisplaygraphrange",
            "-no-display-graph-range", "--no-display-graph-range",
            action="store_false", dest="graphs_disp_range",
            help=help_all("don't display trial range envelopes ongraphs"))
    parser.add_argument("-graphsgendefault", "--graphsgendefault",
            "-graphs-gen-default", "--graphs-gen-default", action="store",
            choices=["avg", "min", "max", "med"], default="avg",
            dest="graphs_gen_default",
            help=help_all("set default method to reduce multiple trials"))
    parser.add_argument("-startdate", "--startdate", action="store",
            dest="start_date", metavar="<MM/DD/YY>",
            help="set graph start date")
    parser.add_argument("-gengraphopts", "--gengraphopts", "-genGraphOpts",
            "--genGraphOpts", action="store", dest="gen_graph_opts",
            default="", help=help_all("set additional graph gen options"))
    # only compile
    parser.add_argument("-comp-only", "--comp-only", action="store_true",
            dest="comp_only", help="only compile the tests, don't run")
    # num locales
    parser.add_argument("-numlocales", "--numlocales", action="store",
            dest="num_locales", default="0",
            help="set the number of locales to run on")
    # run tests that specify numlocales > 1
    parser.add_argument("-max-locales", "--max-locales",
            action="store", dest="max_locales", default="0",
            help="Maximum number of locales to use")
    # run tests that specify numlocales > 1
    parser.add_argument("-multilocale-only", "--multilocale-only",
            action="store_true", dest="multilocale_only",
            help="Only run multilocale test (ignored for comm none)")
    # stdin redirect
    parser.add_argument("-nostdinredirect", "--nostdinredirect",
            action="store_true", dest="no_stdin_redirect",
            help=help_all("don't redirect stdin from /dev/null"))
    parser.add_argument("-stdinredirect", "--stdinredirect",
            action="store_true", dest="stdin_redirect",
            help=help_all("force stdin redirection from /dev/null"))
    # launcher timeout
    parser.add_argument("-launchertimeout", "--launchertimeout",
            action="store", dest="launcher_timeout",
            help=help_all("rely on the launcher to enforce the timeout"))
    # no chpl home warning
    parser.add_argument("-no-chpl-home-warn", "--no-chpl-home-warn",
            action="store_false", dest="chpl_home_warn",
            help=help_all("don't warn about not starting in CHPL_HOME"))
    # progress
    parser.add_argument("-progress", "--progress", action="store_true",
            dest="progress", help=help_all("Log pass/fail for each test"))
    # test root
    parser.add_argument("-test-root", "--test-root", action="store",
        dest="test_root_dir", help=help_all("set abs path to test directory"))
    # jUnit
    parser.add_argument("-junit-xml", "--junit-xml", action="store_true",
            dest="junit_xml", help=help_all("create jUnit XML report"))
    parser.add_argument("-junit-xml-file", "--junit-xml-file", action="store",
            dest="junit_xml_file", metavar="<file>",
            help=help_all("set the path to store the jUnit XML report"))
    parser.add_argument("-junit-remove-prefix", "--junit-remove-prefix",
            action="store", dest="junit_remove_prefix", metavar="<prefix>",
            help=help_all("<prefix> to remove from tests in jUnit report"))
    # respect skipifs
    parser.add_argument("-respect-skipifs", "--respect-skipifs",
            action="store_true", dest="respect_skipifs",
            help="respect '.skipif' files even when testing individual files")
    # respect notests
    parser.add_argument("-respect-notests", "--respect-notests",
            action="store_true", dest="respect_notests",
            help="respect '.notest' files even when testing individual files")
    # extra help
    parser.add_argument("-help", action="help", help=argparse.SUPPRESS)
    parser.add_argument("--help-all", action="help",
            help="show all options, including ones meant for other scripts")

    return parser


# argparse interprets anything that starts with a `--` as an option, so
# something like `--compopts --fast` would get interpreted as two options. Here
# we preprocess to turn that into `--compopts '--fast'` (note that we'll have
# to remove the quotes later)
def preprocess_options():
    index = 0
    for arg in sys.argv:
        arg = arg.replace('\"', '').replace('\'', '')
        if arg in ["-compopts", "--compopts", "-execopts", "--execopts"]:
            escape_next_argument(sys.argv, index)
        index += 1

def escape_next_argument(args, index):
    if index + 1 < len(args):
        next = args[index + 1].strip()
        if next and next[0] == "-": # single argument, not escaped
            args[index + 1] = "'{0}'".format(next)
        print(args)


# UTILITY ROUTINES AND CLASSES

class Logger():
    def __init__(self):
        self.logger = logging.getLogger("start_test")
        self.logger.setLevel(logging.DEBUG)
        # console stdout handlers
        self.console_out = StreamHandlerWithException(sys.stdout)
        # file handlers
        self.file_out = FileHandlerWithException(tmp_log_file, mode="w")
        # add them
        self.logger.addHandler(self.console_out)
        self.logger.addHandler(self.file_out)

    def write(self, msg=" "):
        self.logger.info(msg.rstrip())

    def flush(self):
        self.console_out.flush()
        self.file_out.flush()

    def stop(self):
        self.file_out.flush()
        self.file_out.close()
        self.logger.removeHandler(self.file_out)

    def restart(self):
        self.file_out = FileHandlerWithException(tmp_log_file, mode="a")
        self.logger.addHandler(self.file_out)


# Override the error handling in Python's built-in logging module, to
# make sure remotely-executed builds fail if something goes wrong with
# the infrastructure (network connections, for example).
# Normally, Python logging ignores such errors, on the assumption that
# missing log messages are not very important to the application.
# In this program, however, the logging provides an essential function.

class StreamHandlerWithException(logging.StreamHandler):
    """
    Same as StreamHandler except it raises an exception.
    """

    def handleError(self, record):
        logging.StreamHandler.handleError(self, record)
        raise OSError("fatal error, logging.StreamHandler")

class FileHandlerWithException(logging.FileHandler):
    """
    Same as FileHandler except it raises an exception.
    """

    def handleError(self, record):
        logging.FileHandler.handleError(self, record)
        raise OSError("fatal error, logging.FileHandler")

class CommandError(Exception):
    def __init__(self, retcode, cmd, output=None):
        self.retcode = retcode
        self.cmd = cmd
        self.output = output
    def __str__(self):
        return "Command '{0}' returned non-zero exit status {1}".format(self.cmd, self.retcode)
def run_command(cmd, stderr=None):
    """
    check_output like wrapper, but we're still committed to 2.6 so can't rely
    on check_output
    """
    process = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=stderr)
    output, _ = process.communicate()
    retcode = process.poll()
    if retcode:
        raise CommandError(retcode, cmd, output)

    if sys.version_info[0] >= 3 and not isinstance(output, str):
        output = str(output, 'utf-8')

    return output

def process_skipif_output(output):
    lines = output.splitlines()
    if len(lines) == 0:
      return output
    elif len(lines) > 1:
      print("\n".join(lines[:-1]))
    output = lines[-1]
    return output

def run_git_command(command):
    try:
        output = run_command(["git"]+command, stderr=subprocess.STDOUT).strip()
    except:
        output = None
    return output

def printout(so):
    while True:
        line = so.readline()
        if not line:
            break
        if sys.version_info[0] >= 3 and not isinstance(line, str):
            line = str(line, 'utf-8')
        logger.write(line) # strip default newline
        logger.flush()

@contextlib.contextmanager
def cd(path):
    old_dir = os.getcwd()
    os.chdir(path)
    os.environ["PWD"] = path
    try:
        yield
    finally:
        os.chdir(old_dir)
        os.environ["PWD"] = old_dir


if __name__ == "__main__":
    main()
