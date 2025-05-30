import Sort;
use MRA;
use MadAnalytics;

config const k      = 5;
config const thresh = 1e-5;

proc main() {
    var npt = 10;

    writeln("Mad Chapel -- Show me the boxes\n");

    var fcn  = new unmanaged Fn_Test1();
    var dfcn = new unmanaged Fn_dTest1();

    var F = new unmanaged Function(k=k, thresh=thresh, f=fcn);

    writeln("F.norm2() = ", F.norm2());
    F.summarize();
    F.evalNPT(npt);

    writeln("\nDumping the boxes at each level:");
    
    for lvl in 0..F.max_level { 
        write("\n **",lvl,": ");
        for (n, l) in Sort.sorted(F.s.indices) do
            if n == lvl then write(" [",n,", ",l,"]");
    }
    writeln();

    delete fcn;
    delete dfcn;
    delete F;
}
