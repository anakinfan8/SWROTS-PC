// Exports user-defined names, function signatures and comments as TSV.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.PrintWriter;

public class ExportUserSymbols extends GhidraScript {
    @Override
    public void run() throws Exception {
        String out = getScriptArgs()[0];
        try (PrintWriter w = new PrintWriter(out, "UTF-8")) {
            SymbolTable st = currentProgram.getSymbolTable();
            int functions = 0, userFunctions = 0, labels = 0;
            for (Symbol s : st.getAllSymbols(true)) {
                SourceType src = s.getSource();
                if (src == SourceType.DEFAULT || src == SourceType.ANALYSIS)
                    continue;
                String kind = s.getSymbolType().toString();
                String extra = "";
                if (s.getSymbolType() == SymbolType.FUNCTION) {
                    Function f = getFunctionAt(s.getAddress());
                    if (f != null)
                        extra = f.getPrototypeString(false, false);
                    userFunctions++;
                } else {
                    labels++;
                }
                w.println("SYM\t" + s.getAddress() + "\t" + kind + "\t" + src + "\t" + s.getName(true) + "\t" + extra);
            }
            for (Function f : currentProgram.getFunctionManager().getFunctions(true))
                functions++;
            Listing listing = currentProgram.getListing();
            int[] types = { CodeUnit.PLATE_COMMENT, CodeUnit.PRE_COMMENT, CodeUnit.EOL_COMMENT, CodeUnit.POST_COMMENT,
                CodeUnit.REPEATABLE_COMMENT };
            int comments = 0;
            for (int t : types) {
                var it = listing.getCommentAddressIterator(t, currentProgram.getMemory(), true);
                while (it.hasNext()) {
                    var a = it.next();
                    String c = listing.getComment(t, a);
                    if (c != null) {
                        w.println("CMT\t" + a + "\t" + t + "\t" + c.replace("\n", "\\n").replace("\t", " "));
                        comments++;
                    }
                }
            }
            w.println("SUMMARY\tfunctions=" + functions + "\tuserFunctions=" + userFunctions + "\tuserLabels=" + labels
                + "\tcomments=" + comments);
        }
    }
}
