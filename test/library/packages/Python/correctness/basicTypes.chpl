use Python;
import Reflection;
import List;
import Set;
import Map;

config const print = false;


proc roundTripFunction(func: borrowed) {

  proc testType(type t, value: t) {
    if print then writeln("  type: ", t:string, " value: ", value);
    var res = func(t, value);
    if print then writeln("    res: ", res);
    assert(res == value);
  }

  testType(int(8), 42);
  testType(int(16), 42);
  testType(int(32), 42);
  testType(int(64), 42);
  testType(uint(8), 42);
  testType(uint(16), 42);
  testType(uint(32), 42);
  testType(uint(64), 42);
  testType(real(32), 3.14);
  testType(real(64), 3.14);
  testType(string, "hello");
  testType(bool, true);
  testType(bytes, b"hello");

  var tup1: (int, string, bool) = (17, "blah", false);
  testType(tup1.type, tup1);
  var tup2: 4*int = (5, 4, 3, 2);
  testType(tup2.type, tup2);
  var tup3: (2*int, bool) = ((11, 5), true);
  testType(tup3.type, tup3);

  var arr: [0..<10] int = 0..<10;
  testType(arr.type, arr);
  var l = new List.list(arr);
  testType(l.type, l);
  var s = new Set.set(int);
  for i in arr {
    s.add(i);
  }
  testType(s.type, s);
  var m = new Map.map(int, int);
  for i in arr {
    m.add(i, i*2);
  }
  testType(m.type, m);

}
proc roundTripClass(clsType: borrowed) {

  proc testType(type t, value: t, other: t) {
    var res: t;

    if print then writeln("  type: ", t:string, " value: ", value);
    var obj = clsType(value);
    if print then writeln("    obj: ", obj);

    res = obj.get(t, "value");
    if print then writeln("    obj.value: ", res);
    assert(res == value);

    obj.set("value", other);
    res = obj.call(t, "getter");
    if print then writeln("    obj.getter(): ", res);
    assert(res == other);

    obj.call("setter", value);
    res = obj.get(t, "value");
    if print then writeln("    obj.value: ", res);
    assert(res == value);

  }

  testType(int(8), 42, 43);
  testType(int(16), 42, 43);
  testType(int(32), 42, 43);
  testType(int(64), 42, 43);
  testType(uint(8), 42, 43);
  testType(uint(16), 42, 43);
  testType(uint(32), 42, 43);
  testType(uint(64), 42, 43);
  testType(real(32), 3.14, 2.71);
  testType(real(64), 3.14, 2.71);
  testType(string, "hello", "world");

  /* Tuples in Python are immutable.  The C interface *can* mutate them, but
     only if it knows it owns the full instance.  There's probably a way to
     adjust the implementation so that this is supported, but for now, it
     doesn't work.
  var tup1: (int, string, bool) = (17, "blah", false);
  var tup2: (int, string, bool);
  testType(tup1.type, tup1, tup2);
  var tup3: 4*int = (5, 4, 3, 2);
  var tup4: 4*int;
  testType(tup3.type, tup3, tup4);
  */

  var arr: [0..<10] int = 0..<10;
  var otherArr = [arr.domain] 2;
  testType(arr.type, arr, otherArr);
  var l = new List.list(arr);
  testType(l.type, l, new List.list([arr.domain] 2));
  var s = new Set.set(int);
  for i in arr {
    s.add(i);
  }
  testType(s.type, s, new Set.set(int));

  var m = new Map.map(int, int);
  for i in arr {
    m.add(i, i*2);
  }
  testType(m.type, m, new Map.map(int, int));
}

proc main() {
  var interp = new Interpreter();

  var modName = Reflection.getModuleName();
  var m = interp.importModule(modName);
  if print then writeln("module: ", m);

  var func = m.get("round_trip");
  if print then writeln("func: ", func);
  roundTripFunction(func);

  var clsType = m.get("RoundTrip");
  if print then writeln("class: ", clsType);
  roundTripClass(clsType);

}
