// Applies swrots.map (tools/symbols) to the open SWROTS program.
// Only functions that still have Ghidra's default name (FUN_xxxxxxxx) are renamed; names you
// gave are never changed. Each renamed function gets a plate comment with the evidence level.
// Run from the Script Manager; it asks for swrots.map.
//@category SWROTS
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.CodeUnit;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Namespace;
import ghidra.program.model.symbol.SourceType;
import ghidra.app.util.NamespaceUtils;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;

public class ApplySwrotsSymbols extends GhidraScript {
    @Override
    public void run() throws Exception {
        File map = askFile("Choose swrots.map", "Apply");
        int maxLevel = askInt("Highest evidence level to apply", "1 = official only, 2 = strong matches, 3 = all");
        int renamed = 0, kept = 0;
        try (BufferedReader r = new BufferedReader(new FileReader(map))) {
            String line;
            while ((line = r.readLine()) != null) {
                if (line.startsWith("#"))
                    continue;
                String[] f = line.split("\t", -1);
                if (f.length < 7 || !f[1].equals("function"))
                    continue;
                int level = Integer.parseInt(f[2]);
                String name = f[3];
                if (level > maxLevel || name.startsWith("sub_") || name.contains("::vfunc_"))
                    continue;
                Address a = toAddr(Long.parseLong(f[0], 16));
                Function fn = getFunctionAt(a);
                if (fn == null)
                    fn = createFunction(a, null);
                if (fn == null)
                    continue;
                if (fn.getSymbol().getSource() != SourceType.DEFAULT) {
                    kept++;
                    continue;
                }
                String simple = name;
                Namespace ns = currentProgram.getGlobalNamespace();
                int sep = name.lastIndexOf("::");
                if (sep > 0) {
                    ns = NamespaceUtils.createNamespaceHierarchy(name.substring(0, sep), null, currentProgram,
                        SourceType.IMPORTED);
                    simple = name.substring(sep + 2);
                }
                fn.setParentNamespace(ns);
                fn.setName(simple, SourceType.IMPORTED);
                String note = "[swrots.map level " + level + "] " + f[6];
                currentProgram.getListing().setComment(a, CodeUnit.PLATE_COMMENT, note);
                renamed++;
            }
        }
        println("Renamed " + renamed + " functions; kept " + kept + " existing names.");
    }
}
