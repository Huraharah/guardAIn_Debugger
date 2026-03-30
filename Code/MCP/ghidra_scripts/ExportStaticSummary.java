// Ghidra postScript: export a compact static summary for LLM / GDB planning.
// Usage (headless): -scriptPath <this_dir> -postScript ExportStaticSummary.java <output_json_path>
// @category guardAInDBG
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.*;

import java.io.FileWriter;
import java.util.*;

public class ExportStaticSummary extends GhidraScript {

    private static String esc(String s) {
        if (s == null) {
            return "";
        }
        return s.replace("\\", "\\\\").replace("\"", "\\\"").replace("\n", "\\n").replace("\r", "\\r");
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) {
            println("ExportStaticSummary: missing output path argument");
            return;
        }
        String outPath = args[0];
        Listing listing = currentProgram.getListing();
        SymbolTable symTab = currentProgram.getSymbolTable();

        StringBuilder json = new StringBuilder();
        json.append("{\n");
        json.append("  \"program\": \"").append(esc(currentProgram.getName())).append("\",\n");
        json.append("  \"language\": \"").append(esc(currentProgram.getLanguageID().toString())).append("\",\n");
        Address entry = currentProgram.getImageBase();
        try {
            entry = currentProgram.getMinAddress();
        } catch (Exception ignored) {
        }
        json.append("  \"image_base\": \"").append(entry != null ? entry.toString() : "").append("\",\n");

        json.append("  \"memory_blocks\": [\n");
        MemoryBlock[] blocks = currentProgram.getMemory().getBlocks();
        int bi = 0;
        for (MemoryBlock b : blocks) {
            if (bi++ > 0) {
                json.append(",\n");
            }
            json.append("    {\"name\": \"").append(esc(b.getName())).append("\", ");
            json.append("\"start\": \"").append(b.getStart().toString()).append("\", ");
            json.append("\"size\": ").append(b.getSize()).append(", ");
            json.append("\"execute\": ").append(b.isExecute()).append(", ");
            json.append("\"write\": ").append(b.isWrite()).append("}");
        }
        json.append("\n  ],\n");

        json.append("  \"imports\": [\n");
        int ii = 0;
        for (Symbol s : symTab.getExternalSymbols()) {
            if (ii >= 256) {
                break;
            }
            if (ii++ > 0) {
                json.append(",\n");
            }
            json.append("    \"").append(esc(s.getName())).append("\"");
        }
        json.append("\n  ],\n");

        json.append("  \"functions\": [\n");
        FunctionManager fm = currentProgram.getFunctionManager();
        int fi = 0;
        for (Function f : fm.getFunctions(true)) {
            if (fi >= 200) {
                break;
            }
            if (fi++ > 0) {
                json.append(",\n");
            }
            json.append("    {\"name\": \"").append(esc(f.getName())).append("\", ");
            json.append("\"entry\": \"").append(f.getEntryPoint().toString()).append("\"}");
        }
        json.append("\n  ],\n");

        json.append("  \"strings\": [\n");
        int si = 0;
        DataIterator dit = listing.getDefinedData(true);
        while (dit.hasNext() && si < 400 && !monitor.isCancelled()) {
            Data d = dit.next();
            Object raw = d.getValue();
            if (!(raw instanceof String)) {
                continue;
            }
            String val = ((String) raw).trim();
            if (val.isEmpty()) {
                continue;
            }
            if (val.length() < 4) {
                continue;
            }
            String low = val.toLowerCase();
            if (!(low.contains("flag") || low.contains("xor") || low.contains("aes") || low.contains("crypt")
                    || low.contains("password") || low.contains("usage") || low.contains("decrypt")
                    || low.contains("encrypt") || low.contains("key"))) {
                continue;
            }
            if (si++ > 0) {
                json.append(",\n");
            }
            json.append("    \"").append(esc(val.length() > 200 ? val.substring(0, 200) + "..." : val))
                .append("\"");
        }
        json.append("\n  ]\n");
        json.append("}\n");

        try (FileWriter fw = new FileWriter(outPath)) {
            fw.write(json.toString());
        }
        println("ExportStaticSummary wrote " + outPath);
    }
}
