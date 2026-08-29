#include "Masm.h"

#include "../Mangle.h"

#include <cstdio>
#include <cstdlib>
#include <ostream>
#include <string>
#include <vector>

namespace {

[[noreturn]] void give_up(const std::string &what, const std::string &why) {
    std::fprintf(stderr, "cc1: masm: %s\n  for: %s\n", why.c_str(),
                 what.c_str());
    std::exit(1);
}

bool isReservedInMasm(const std::string &name) {
    static const char *const kReserved[] = {
        "ah","al","ax","eax","rax","bh","bl","bx","ebx","rbx",
        "ch","cl","cx","ecx","rcx","dh","dl","dx","edx","rdx",
        "si","esi","rsi","di","edi","rdi","bp","ebp","rbp","sp","esp","rsp",
        "r8","r9","r10","r11","r12","r13","r14","r15",
        "cs","ds","es","fs","gs","ss","st","flat","eip","rip",
        "byte","word","dword","qword","tbyte","oword","ptr","offset",
        "length","lengthof","size","sizeof","type","typedef","this",
        "near","far","short","proc","endp","segment","ends","assume",
        "public","extern","externdef","end","align","even","org","dup",
        "mask","width","low","high","lowword","highword",
        "and","or","xor","not","mod","shl","shr","eq","ne","lt","le","gt","ge",
        "if","else","endif","macro","endm","rept","irp","exitm","local",
        "label","comment","include","includelib","name","group","record",
        "struc","struct","union","db","dw","dd","dq","dt","page","title",

        "aaa","aad","aam","aas","adc","add","bound","bsf","bsr","bt","btc",
        "btr","bts","call","cbw","cdq","clc","cld","cli","cmc","cmp","cmps",
        "cmpsb","cmpsd","cmpsw","cqo","cwd","cwde","daa","das","dec","div",
        "enter","esc","hlt","idiv","imul","in","inc","ins","int","into","iret",
        "ja","jae","jb","jbe","jc","jcxz","je","jg","jge","jl","jle","jmp",
        "jna","jnb","jnc","jne","jng","jnl","jno","jnp","jns","jnz","jo","jp",
        "jpe","jpo","js","jz","lahf","lds","lea","leave","les","lock","lods",
        "lodsb","lodsd","lodsw","loop","loope","loopne","loopnz","loopz",
        "mov","movs","movsb","movsd","movss","movsw","movsx","movsxd","movzx",
        "mul","neg","nop","out","outs","pop","popa","popf","push","pusha",
        "pushf","rcl","rcr","rep","repe","repne","repnz","repz","ret","retf",
        "retn","rol","ror","sahf","sal","sar","sbb","scas","scasb","scasd",
        "scasw","seta","setae","setb","setbe","sete","setg","setge","setl",
        "setle","setna","setne","setnz","setz","sgdt","shld","shrd","sidt",
        "stc","std","sti","stos","stosb","stosd","stosw","sub","test","wait",
        "xadd","xchg","xlat","xlatb",
        "fabs","fadd","fchs","fcos","fdiv","fild","finit","fist","fld","fmul",
        "fnop","fprem","fptan","frndint","fscale","fsin","fsqrt","fst","fstp",
        "fsub","ftst","fwait","fxam","fxch",
        "addpd","addps","addsd","addss","andpd","andps","cvtsd2ss","cvtsi2sd",
        "cvtss2sd","cvttsd2si","divsd","divss","maxsd","minsd","movapd","movaps",
        "movd","movq","mulsd","mulss","orpd","orps","pxor","sqrtsd","subsd",
        "subss","ucomisd","ucomiss","xorpd","xorps",
        nullptr,
    };
    std::string lower;
    for (char c : name) lower += static_cast<char>(std::tolower(c));
    for (const char *const *p = kReserved; *p != nullptr; ++p)
        if (lower == *p) return true;
    return false;
}

bool canUnreserve(const std::string &name) {
    static const char *const kNeverEmitted[] = {
        "fabs","fadd","fchs","fcos","fdiv","fild","finit","fist","fld","fmul",
        "fnop","fprem","fptan","frndint","fscale","fsin","fsqrt","fst","fstp",
        "fsub","ftst","fwait","fxam","fxch",
        nullptr,
    };
    std::string lower;
    for (char c : name) lower += static_cast<char>(std::tolower(c));
    for (const char *const *p = kNeverEmitted; *p != nullptr; ++p)
        if (lower == *p) return true;
    return false;
}

const char *ptrFor(int bytes) {
    switch (bytes) {
    case 1: return "BYTE PTR ";
    case 2: return "WORD PTR ";
    case 4: return "DWORD PTR ";
    case 8: return "QWORD PTR ";
    default: return "";
    }
}

struct Rule {
    const char *att;
    const char *masm;
    int width;

};

const Rule kRules[] = {
    { "movb", "mov", 1 }, { "movw", "mov", 2 },
    { "movl", "mov", 4 }, { "movq", "mov", 8 },
    { "mov",  "mov", 0 }, { "movabs", "mov", 0 },
    { "movslq", "movsxd", 4 },
    { "movsbq", "movsx", 1 }, { "movswq", "movsx", 2 },

    { "movzbq", "movzx", 1 }, { "movzwq", "movzx", 2 },
    { "movzbl", "movzx", 1 }, { "movzwl", "movzx", 2 },

    { "lea", "lea", 0 },
    { "push", "push", 0 }, { "pop", "pop", 0 },

    { "add", "add", 0 }, { "sub", "sub", 0 }, { "imul", "imul", 0 },
    { "idiv", "idiv", 0 }, { "div", "div", 0 }, { "neg", "neg", 0 },
    { "and", "and", 0 }, { "or", "or", 0 }, { "xor", "xor", 0 },
    { "shl", "shl", 0 }, { "shr", "shr", 0 }, { "sar", "sar", 0 },
    { "cmp", "cmp", 0 }, { "cdq", "cdq", 0 }, { "cqo", "cqo", 0 },
    { "addl", "add", 4 }, { "cmpl", "cmp", 4 }, { "testb", "test", 1 },

    { "call", "call", 0 }, { "ret", "ret", 0 },
    { "jmp", "jmp", 0 }, { "je", "je", 0 }, { "jne", "jne", 0 },
    { "jae", "jae", 0 }, { "jns", "jns", 0 },

    { "sete", "sete", 0 }, { "setne", "setne", 0 },
    { "setl", "setl", 0 }, { "setle", "setle", 0 },
    { "setg", "setg", 0 }, { "setge", "setge", 0 },
    { "seta", "seta", 0 }, { "setae", "setae", 0 },
    { "setb", "setb", 0 }, { "setbe", "setbe", 0 },
    { "setp", "setp", 0 }, { "setnp", "setnp", 0 },

    { "movsd", "movsd", 8 }, { "movss", "movss", 4 },
    { "movapd", "movapd", 0 }, { "movaps", "movaps", 0 },
    { "movd", "movd", 0 },
    { "addsd", "addsd", 0 }, { "subsd", "subsd", 0 },
    { "mulsd", "mulsd", 0 }, { "divsd", "divsd", 0 },
    { "addss", "addss", 0 }, { "subss", "subss", 0 },
    { "mulss", "mulss", 0 }, { "divss", "divss", 0 },
    { "ucomisd", "ucomisd", 0 }, { "ucomiss", "ucomiss", 0 },
    { "pxor", "pxor", 0 }, { "xorpd", "xorpd", 0 }, { "xorps", "xorps", 0 },
    { "cvtsi2sdq", "cvtsi2sd", 8 }, { "cvtsi2ssq", "cvtsi2ss", 8 },
    { "cvttsd2si", "cvttsd2si", 0 }, { "cvttss2si", "cvttss2si", 0 },
    { "cvtss2sd", "cvtss2sd", 0 }, { "cvtsd2ss", "cvtsd2ss", 0 },
};

const Rule *ruleFor(const std::string &m) {
    for (const Rule &r : kRules)
        if (m == r.att) return &r;
    return nullptr;
}

}

std::string MasmSpelling::mangle(const std::string &name) {
    if (name.find('.') != std::string::npos) {
        std::string out = "$";
        for (char c : name) out += (c == '.') ? '_' : c;
        return out;
    }
    if (!isReservedInMasm(name)) return name;
    if (defined_.find(name) != defined_.end()) return "$" + name;
    if (!canUnreserve(name))
        give_up(name, "'" + name + "' is a name ml64 reserves and this file "
                      "emits, so an imported symbol cannot be spelled it");
    unreserved_.insert(name);
    return name;
}

MasmSpelling::Rendered MasmSpelling::render(const Op &x) {
    switch (x.kind) {
    case Op::Reg: {

        Str name = x.text.substr(1);
        return { std::string(name), false, false, name.startsWith("xmm") };
    }
    case Op::Imm: {
        if (!x.immNumeric) return { std::string(x.text), false, true, false };
        std::string v = x.immNeg ? "-" + std::to_string(x.uimm)
                                 : std::to_string(x.uimm);
        return { v, false, true, false };
    }
    case Op::Mem: {

        std::string t = "[" + std::string(x.text.substr(1));
        long long d = x.hasDisp ? x.disp : 0;
        // **The whole frame moved, so every offset into it moves with it.**
        // The code generator writes frame operands against rbp as Itanium
        // establishes it - taken before the allocation, with the locals below
        // it - and this target takes rbp after the allocation instead, so its
        // rbp is exactly frameSize lower. Every `[rbp + d]` therefore becomes
        // `[rbp + d + frameSize]`, and that is true of the positive ones as
        // well: an incoming stack argument is above the old rbp and is above
        // the new one by the same amount plus the frame.
        //
        // Done here rather than where operands are built because there are
        // several dozen of those and they compute their displacements in
        // half a dozen different shapes - one of which is all it takes to
        // miss, and the miss is a program that reads the wrong stack slot.
        if (std::string(x.text.substr(1)) == "rbp") d += frameSize_;
        if (d != 0) {
            if (d < 0) t += std::to_string(d);
            else       t += "+" + std::to_string(d);
        }
        t += "]";
        return { t, true, false, false };
    }
    case Op::Rip: {

        std::string sym(x.text);
        referenced_.insert(sym);
        return { "[" + mangle(sym) + "]", true, false, false };
    }
    case Op::Ind:
        return { std::string(x.text.substr(1)), false, false, false };
    case Op::Lbl: {
        std::string sym(x.text);
        referenced_.insert(sym);
        return { mangle(sym), false, false, false };
    }
    }
    return {};
}

void MasmSpelling::ins(const std::string &m) {
    const Rule *r = ruleFor(m);
    if (r == nullptr) give_up(m, "an instruction this spelling does not know");
    o_ += "  "; o_ += r->masm; o_ += '\n';
}

void MasmSpelling::ins(const std::string &m, const Op &a) {
    const Rule *r = ruleFor(m);
    if (r == nullptr) give_up(m, "an instruction this spelling does not know");
    Rendered x = render(a);
    std::string t = x.text;
    if (x.isMem && r->width != 0) t = std::string(ptrFor(r->width)) + t;
    o_ += "  "; o_ += r->masm; o_ += ' '; o_ += t; o_ += '\n';
}

void MasmSpelling::ins(const std::string &m, const Op &a, const Op &b) {
    const Rule *r = ruleFor(m);
    if (r == nullptr) give_up(m, "an instruction this spelling does not know");

    Rendered src = render(a), dst = render(b);

    std::string name = r->masm;
    if (m == "movq" && (src.isXmm || dst.isXmm)) name = "movq";

    std::string sTxt = src.text, dTxt = dst.text;
    if (r->width != 0) {
        if (src.isMem) sTxt = std::string(ptrFor(r->width)) + sTxt;
        else if (dst.isMem && m != "movsxd")
            dTxt = std::string(ptrFor(r->width)) + dTxt;
    }

    if (r->width == 0 && dst.isMem && src.isImm)
        give_up(m, "a store of an immediate with no width to infer");

    o_ += "  "; o_ += name; o_ += ' '; o_ += dTxt; o_ += ", "; o_ += sTxt; o_ += '\n';
}

void MasmSpelling::defLabel(const std::string &l) {
    if (seg_ == Code) {

        if (l.compare(0, 3, ".L.") != 0)
            give_up(l, "a label in code that is not an internal one");
        defined_.insert(l);
        o_ += mangle(l); o_ += ":\n";
        return;
    }
    defined_.insert(l);
    flushPending();
    pending_ = mangle(l);
}

void MasmSpelling::functionBegin(const std::string &name, bool exported) {
    defined_.insert(name);
    if (exported) exported_.insert(name);
    flushPending();
    if (seg_ != Code) { o_ += "\n.CODE\n"; seg_ = Code; }

    // **`PROC`, not `PROC FRAME`, and the unwind data written by hand.**
    //
    // PROC FRAME is the easy way to get .pdata and .xdata, and it is a dead
    // end: a function with an exception handler carries the handler's address
    // *and its data* inside the same UNWIND_INFO, and no MASM directive
    // reaches the second of those. cl does not use PROC FRAME either - it
    // writes $pdata$ and $unwind$ itself, which is what this does now.
    //
    // Done for every function rather than only the ones with a handler on
    // purpose: one path, exercised by the whole suite, rather than a second
    // one that only the new feature walks.
    fnName_ = name;
    o_ += mangle(name); o_ += " PROC\n";
    o_ += "$LNbeg$"; o_ += mangle(name); o_ += ":\n";
}

void MasmSpelling::raw(const std::string &text) {
    flushPending();
    o_ += text;
}

void MasmSpelling::prologue(int frameSize, const std::string &lsda) {
    // The caller passes a non-empty name when the function has handlers. What
    // it *is* does not matter here - Itanium's exception-table label means
    // nothing to MASM - only that there is one, which decides the flags in
    // the unwind header and whether a FuncInfo follows the codes.
    hasEh_ = !lsda.empty();
    frameSize_ = frameSize;
    const std::string m = mangle(fnName_);

    // **The frame pointer is taken *after* the allocation on this target**,
    // which is the whole difference from the Itanium prologue and is not a
    // preference. The Microsoft runtime hands a handler the "establisher
    // frame", computed as rbp minus the frame offset in the unwind header,
    // and every displacement in an FH3 table is an unsigned offset *up* from
    // it. Taking rbp first, as Itanium does, puts every local below that
    // point where no table can name it - measured against cl, which writes
    // `sub rsp,N` and only then establishes its frame pointer.
    o_ += "  push rbp\n";
    o_ += "$LNpush$" + m + ":\n";
    if (frameSize > 0) {
        o_ += "  sub rsp, "; appendNum(o_, frameSize); o_ += '\n';
    }
    o_ += "$LNalloc$" + m + ":\n";
    o_ += "  mov rbp, rsp\n";
    o_ += "$LNprolog$" + m + ":\n";

    // Last instruction first, which is the order an unwinder undoes them in.
    // That is now SET_FPREG, then the allocation, then the push.
    unwindCodes_ = 0;
    unwindData_.clear();
    // UWOP_SET_FPREG is 3; the frame offset lives in the header, and it is
    // zero because rbp is set to rsp exactly.
    unwindData_ += "  DB $LNprolog$" + m + "-$LNbeg$" + m + "\n";
    unwindData_ += "  DB 03H\n";
    unwindCodes_ += 1;
    // UWOP_ALLOC_SMALL is 2, with (size/8 - 1) in the high nibble, and
    // reaches 128 bytes; past that UWOP_ALLOC_LARGE is 1 with a slot of its
    // own holding size/8.
    if (frameSize > 0) {
        unwindData_ += "  DB $LNalloc$" + m + "-$LNbeg$" + m + "\n";
        if (frameSize <= 128 && frameSize % 8 == 0) {
            char buf[8];
            std::snprintf(buf, sizeof buf, "%02XH",
                          ((frameSize / 8 - 1) << 4) | 2);
            unwindData_ += "  DB 0"; unwindData_ += buf; unwindData_ += "\n";
            unwindCodes_ += 1;
        } else {
            unwindData_ += "  DB 01H\n";
            unwindData_ += "  DW " + std::to_string((frameSize + 7) / 8) + "\n";
            unwindCodes_ += 2;
        }
    }
    // UWOP_PUSH_NONVOL is 0 with the register in the high nibble - rbp is 5.
    unwindData_ += "  DB $LNpush$" + m + "-$LNbeg$" + m + "\n";
    unwindData_ += "  DB 050H\n";
    unwindCodes_ += 1;
}

// **Every offset here is a label difference, not a counted byte.** An unwind
// code records where in the prologue its instruction *ends*, and `sub rsp, 40`
// is four bytes where `sub rsp, 400` is seven - the assembler chooses, so the
// assembler is asked. Counting them here would be a second encoder that has
// to agree with ml64 forever.
void MasmSpelling::functionEnd(const std::string &name) {
    const std::string m = mangle(name);
    // **The dot is the whole of it.** A segment called `pdata` is a segment
    // called pdata; the linker builds the image's exception directory from
    // `.pdata`, and without the dot the runtime finds no unwind record for
    // the frame at all - measured, dumpbin showed `pdata` and `xdata` beside
    // `.text$mn`. cl's listing writes them undotted, which is the third thing
    // in that listing that is a record of what cl means rather than something
    // that assembles.
    o_ += "$LNend$" + m + ":\n";
    o_ += m + " ENDP\n";

    // READONLY and the alignment, or the linker finds two .pdata
    // sections with different attributes and says so.
    o_ += "\n.pdata SEGMENT READONLY ALIGN(4) 'DATA'\n";
    o_ += "$pdata$" + m + " DD imagerel $LNbeg$" + m + "\n";
    o_ += "  DD imagerel $LNend$" + m + "\n";
    o_ += "  DD imagerel $unwind$" + m + "\n";
    o_ += ".pdata ENDS\n";

    // UNWIND_INFO: version 1 with no flags, the prologue's size, how many
    // codes follow, and the frame register - rbp, at offset 0 from where rsp
    // stood when it was set. The codes are last-first, which is the order an
    // unwinder undoes them in.
    o_ += ".xdata SEGMENT READONLY ALIGN(8) 'DATA'\n";
    // Version 1, and the flags in the top five bits. 0x19 is version 1 with
    // UNW_FLAG_EHANDLER and UNW_FLAG_UHANDLER, which is what cl writes for a
    // function with a `try` - measured. Without them the runtime unwinds
    // straight past the frame and never looks at the FuncInfo.
    o_ += "$unwind$" + m + (hasEh_ ? " DB 019H\n" : " DB 01H\n");
    o_ += "  DB $LNprolog$" + m + "-$LNbeg$" + m + "\n";
    o_ += "  DB " + std::to_string(unwindCodes_) + "\n";
    o_ += "  DB 05H\n";
    o_ += unwindData_;
    // The codes are padded to an even count, and the handler pointers go
    // after that padding rather than after the codes.
    if (unwindCodes_ % 2 != 0) o_ += "  DW 0\n";
    if (hasEh_) {
        referenced_.insert("__CxxFrameHandler3");
        o_ += "  DD imagerel __CxxFrameHandler3\n";
        o_ += "  DD imagerel $cppxdata$" + m + "\n";
    }
    o_ += ".xdata ENDS\n\n";
    o_ += ".CODE\n";

    unwindData_.clear();
    unwindCodes_ = 0;
    hasEh_ = false;
}

void MasmSpelling::globl(const std::string &name) { exported_.insert(name); }

void MasmSpelling::textSection() {
    flushPending();
    if (seg_ != Code) { o_ += "\n.CODE\n"; seg_ = Code; }
}

void MasmSpelling::rodataSection() {
    flushPending();
    if (seg_ != Const) { o_ += "\n.CONST\n"; seg_ = Const; }
}

void MasmSpelling::dataSection() {
    flushPending();
    if (seg_ != Data) { o_ += "\n.DATA\n"; seg_ = Data; }
}

void MasmSpelling::bssSection() {
    flushPending();
    if (seg_ != Bss) { o_ += "\n.DATA?\n"; seg_ = Bss; }
}

void MasmSpelling::objectType(const std::string &) {}
void MasmSpelling::objectSize(const std::string &, int) {}

void MasmSpelling::align(int n) {
    flushPending();
    o_ += "  ALIGN "; appendNum(o_, n); o_ += '\n';
}

void MasmSpelling::zero(int n) {
    if (n == 0) return;
    items("DB", { std::to_string(n) +
                  (seg_ == Bss ? " DUP (?)" : " DUP (0)") });
}

void MasmSpelling::dataInt(int size, long long v) {
    const char *dir = size == 1 ? "DB" : size == 2 ? "DW"
                    : size == 4 ? "DD" : "DQ";
    items(dir, { std::to_string(v) });
}

void MasmSpelling::dataSym(const std::string &sym, long long off) {
    std::string t = mangle(sym);
    if (off > 0) t += "+" + std::to_string(off);
    else if (off < 0) t += "-" + std::to_string(-off);
    items("DQ", { t });
}

void MasmSpelling::dataBytes(const std::string &bytes) {
    std::vector<std::string> it;
    it.reserve(bytes.size());
    for (char c : bytes)
        it.push_back(std::to_string(
            static_cast<int>(static_cast<unsigned char>(c))));
    items("DB", it);
}

void MasmSpelling::flushPending() {
    if (!pending_.empty()) {
        o_ += pending_; o_ += " LABEL BYTE\n";
        pending_.clear();
    }
}

void MasmSpelling::items(const char *dir, const std::vector<std::string> &it) {
    const std::size_t kPerLine = 16;
    for (std::size_t i = 0; i < it.size(); i += kPerLine) {
        std::string chunk;
        for (std::size_t j = i; j < it.size() && j < i + kPerLine; j++) {
            if (!chunk.empty()) chunk += ", ";
            chunk += it[j];
        }
        if (i == 0 && !pending_.empty()) {
            o_ += pending_; o_ += ' '; o_ += dir; o_ += ' '; o_ += chunk; o_ += '\n';
            pending_.clear();
        } else {
            o_ += "  "; o_ += dir; o_ += ' '; o_ += chunk; o_ += '\n';
        }
    }
}

void MasmSpelling::predefine(const std::vector<std::string> &names) {
    for (const std::string &n : names) defined_.insert(n);
}

void MasmSpelling::preamble(std::ostream &sink) {
    sink << "; Generated by cc1 for x86_64-windows, in MASM syntax for ml64.\n";
    sink << "; The instruction selection is the same one the Linux backend makes;\n";
    sink << "; only the spelling is Microsoft's. See src/backend/Masm.cpp.\n\n";

    // **A label inside a PROC is local to it unless this says otherwise.**
    // MASM scopes them by default, so the unwind data - which lives outside
    // the procedure and measures into it - could not name the labels the
    // prologue defines. Every label this compiler writes already carries the
    // function's own symbol, so there is nothing for the wider scope to
    // collide with.
    // **DOTNAME lets a segment be called `.pdata`, and NOSCOPED lets the
    // unwind data name the labels inside a procedure.** Both are needed for
    // the same reason: the exception tables live outside the function and
    // measure into it, and MASM's defaults assume nothing does that.
    sink << "OPTION DOTNAME\n";
    sink << "OPTION NOSCOPED\n\n";

    for (const std::string &g : unreserved_)
        sink << "OPTION NOKEYWORD:<" << g << ">\n";
    if (!unreserved_.empty()) sink << "\n";

    for (const std::string &e : exported_)
        sink << "PUBLIC " << mangle(e) << "\n";
    for (const std::string &r : referenced_)
        if (defined_.find(r) == defined_.end())
            sink << "EXTERN " << mangle(r) << ":PROC\n";
    sink << "\n";
}

void MasmSpelling::postamble(std::ostream &sink) {

    if (!pending_.empty())
        give_up(pending_, "a data label left dangling at the end of the file");
    sink << "\nEND\n";
}

// **The chain the Microsoft ABI wants before anything may be thrown**, all of
// it measured from cl's own listing rather than read off a description:
//
//   ??_R0H@8       the RTTI type descriptor - the type_info vftable, a spare
//                  word, and the decorated name, which for a fundamental type
//                  is a '.' and the type's own letter
//   _CT??_R0H@84   one catchable type: properties, the descriptor, where the
//                  object sits, the virtual-base fields it does not use, its
//                  size, and the copy function a scalar does not need
//   _CTA1H         the array of those - a count and one entry
//   _TI1H          the ThrowInfo itself, whose fourth word is the array
//
// Every cross-reference is `imagerel`, which is what makes them relocatable
// inside the image; `ORG $+4` is the padding cl writes for the vbtable field.
// The segments are cl's too: the descriptor in data$r, the rest in xdata$x,
// each COMDAT so that two objects throwing an int fold into one.
// -2 into the runtime's scratch word, which is what says "this frame has not
// entered anything yet". cl writes it as the first instruction of a function
// with a `try`; written here at the head of the guarded range instead, which
// is the same thing for as long as the range it protects is the only one that
// reads it.
void MasmCodeGen::storeUnwindHelp(int slot) {
    masm_.raw("  mov QWORD PTR [rbp+" +
              std::to_string(masm_.frameSize_ - slot) + "], -2\n");
}

// **A funclet is a slice of the ordinary output, lifted.** Walking the handler
// appends its code like any other, so remembering where that began and cutting
// back to it afterwards gives the body exactly - and the code generator needs
// to know nothing about any of this.
std::string MasmCodeGen::beginFunclet() {
    masm_.raw("");                       // nothing pending inside the slice
    funcletMark_ = out_.size();
    funcletSymbol_ = masm_.mangledName() + "$catch$" +
                     std::to_string(funcletIndex_++);
    return funcletSymbol_;
}

// The funclet's own frame, and the two things that make it work: `rdx` is the
// establisher frame - the parent's rsp after its prologue - so adding the
// parent's frame size back recovers the rbp every local was written against,
// and the handler body then compiles as though it were inline. And a funclet
// *returns* the address to carry on at, in rax, rather than jumping there.
void MasmCodeGen::endFunclet(const std::string &resume) {
    masm_.raw("");
    std::string body = out_.substr(funcletMark_);
    out_.resize(funcletMark_);

    const std::string sym = funcletSymbol_;
    std::string f;
    // **`.text$x`, and the dot is the whole of it** - the same trap as
    // `.pdata` in step 1, and found the same way. A segment called `text` is
    // a segment called text: the linker gives it a section of its own with
    // data attributes, and the runtime then calls a handler that sits on a
    // page it may not execute. It faults *at the first instruction of the
    // funclet*, which reads as the dispatch having gone wrong when in fact
    // the dispatch was right and the page was not code.
    //
    // cl's listing writes `text$x` undotted, which is the third time that
    // listing has recorded what cl means rather than something that
    // assembles - see the note on .pdata.
    // The 'CODE' class is what makes the linker give it execute
    // permission and fold it beside .text rather than beside the data.
    f += "\n.text$x SEGMENT ALIGN(16) 'CODE'\n";
    f += sym + " PROC\n";
    f += "$LNbeg$" + sym + ":\n";
    f += "  mov QWORD PTR [rsp+16], rdx\n";
    f += "  push rbp\n";
    f += "$LNpush$" + sym + ":\n";
    f += "  sub rsp, 32\n";
    f += "$LNprolog$" + sym + ":\n";
    // **rdx is the establisher frame, and that is now exactly the parent's
    // rbp** - the two became the same thing when the frame pointer moved to
    // the bottom of the allocation, so the handler body addresses the
    // parent's locals with no adjustment at all.
    f += "  mov rbp, rdx\n";
    f += body;
    f += "  lea rax, " + masm_.labelName(resume) + "\n";
    f += "  add rsp, 32\n";
    f += "  pop rbp\n";
    f += "  ret 0\n";
    f += "$LNend$" + sym + ":\n";
    f += sym + " ENDP\n";
    f += ".text$x ENDS\n";

    // A funclet carries unwind data of its own, naming the same handler and
    // *the parent's* FuncInfo - the two share one description of the try.
    f += "\n.pdata SEGMENT READONLY ALIGN(4) 'DATA'\n";
    f += "$pdata$" + sym + " DD imagerel $LNbeg$" + sym + "\n";
    f += "  DD imagerel $LNend$" + sym + "\n";
    f += "  DD imagerel $unwind$" + sym + "\n";
    f += ".pdata ENDS\n";
    f += ".xdata SEGMENT READONLY ALIGN(8) 'DATA'\n";
    f += "$unwind$" + sym + " DB 019H\n";
    f += "  DB $LNprolog$" + sym + "-$LNbeg$" + sym + "\n";
    f += "  DB 02H\n";
    f += "  DB 00H\n";
    // UWOP_ALLOC_SMALL for 32 bytes: the code is 2 with (size/8 - 1) in the
    // high nibble, so (32/8 - 1) = 3 gives 032H. 042H would claim 40 and
    // leave the runtime unwinding through this funclet to the wrong place.
    f += "  DB $LNprolog$" + sym + "-$LNbeg$" + sym + "\n";
    f += "  DB 032H\n";
    f += "  DB $LNpush$" + sym + "-$LNbeg$" + sym + "\n";
    f += "  DB 050H\n";
    f += "  DD imagerel __CxxFrameHandler3\n";
    f += "  DD imagerel $cppxdata$" + masm_.mangledName() + "\n";
    f += ".xdata ENDS\n";
    funclets_ += f;
}

// The four FH3 tables, written after the function they describe.
//
// **Every offset here is measured from the stack pointer as it stands after
// the prologue** - the establisher frame - and cxx1 writes locals as [rbp-N]
// with rbp taken *before* the frame was allocated. The two are one frame
// apart, so a slot at [rbp-N] is at frameSize-N from the establisher, and
// that subtraction is the whole of the translation. Measured with cl on a
// function that establishes rbp: its $T2 at [rbp+16] with rbp = establisher
// + 32 is dispUnwindHelp 0x30, and 16 + 32 is 48.
void MasmCodeGen::emitExceptionTables(const Function &fn) {
    if (msTries().empty()) {
        if (!callSites().empty()) emitLsda(fn.symbol());
        out_ += funclets_;
        funclets_.clear();
        funcletIndex_ = 0;
        return;
    }

    const std::string m = masm_.mangledName();
    const int frame = masm_.frameSize_;
    const std::size_t tries = msTries().size();
    std::string o;
    o += funclets_;
    funclets_.clear();
    funcletIndex_ = 0;

    // **Two states per try**: the body is one and its handlers the next, so
    // try k owns states 2k and 2k+1 and there are 2*tries of them. They are
    // numbered rather than nested because the parser refuses a `try` inside
    // another - a nested one would want tryLow and tryHigh to span its
    // child's states, which is what those two fields are for.
    const std::size_t states = 2 * tries;

    std::size_t ipRows = 0;
    for (std::size_t k = 0; k < tries; k++)
        ipRows += 2 + msTries()[k].handlers.size();

    o += "\n.xdata SEGMENT READONLY ALIGN(8) 'DATA'\n";
    o += "$cppxdata$" + m + " DD 019930522H\n";
    o += "  DD " + std::to_string(states) + "\n";
    o += "  DD imagerel $stateUnwindMap$" + m + "\n";
    o += "  DD " + std::to_string(tries) + "\n";
    o += "  DD imagerel $tryMap$" + m + "\n";
    o += "  DD " + std::to_string(ipRows + 1) + "\n";
    o += "  DD imagerel $ip2state$" + m + "\n";
    o += "  DD " + std::to_string(frame - msTries()[0].unwindHelpSlot) + "\n";
    o += "  DD 00H\n";                      // no exception specification
    o += "  DD 01H\n";                      // EHFlags: compiled with /EHsc

    // No cleanups yet, so every state unwinds to nothing and runs nothing.
    o += "$stateUnwindMap$" + m;
    for (std::size_t i = 0; i < states; i++)
        o += (i == 0 ? " DD 0ffffffffH\n" : "  DD 0ffffffffH\n") + std::string("  DD 00H\n");

    o += "$tryMap$" + m;
    for (std::size_t k = 0; k < tries; k++) {
        const MsTryRegion &r = msTries()[k];
        o += (k == 0 ? " DD " : "  DD ") + std::to_string(2 * k) + "\n";   // tryLow
        o += "  DD " + std::to_string(2 * k) + "\n";                       // tryHigh
        o += "  DD " + std::to_string(2 * k + 1) + "\n";                   // catchHigh
        o += "  DD " + std::to_string(r.handlers.size()) + "\n";
        o += "  DD imagerel $handlerMap$" + std::to_string(k) + "$" + m + "\n";
    }

    for (std::size_t k = 0; k < tries; k++) {
        const MsTryRegion &r = msTries()[k];
        o += "$handlerMap$" + std::to_string(k) + "$" + m;
        for (std::size_t i = 0; i < r.handlers.size(); i++) {
            const MsHandlerRow &h = r.handlers[i];
            // 0x40 is HT_IsCatchAll, and a catch-all names no type.
            o += (i == 0 ? " DD " : "  DD ");
            o += h.descriptor.empty() ? "040H\n" : "00H\n";
            o += h.descriptor.empty() ? "  DD 00H\n"
                                      : "  DD imagerel " + h.descriptor + "\n";
            o += "  DD " + std::to_string(h.objectSlot == 0
                                              ? 0 : frame - h.objectSlot) + "\n";
            o += "  DD imagerel " + h.funclet + "\n";
            o += "  DD " + std::to_string(frame) + "\n";
        }
    }

    // Where each state begins. -1 is "outside any try", and a funclet is
    // wholly inside its own handler state.
    o += "$ip2state$" + m + " DD imagerel $LNbeg$" + m + "\n";
    o += "  DD 0ffffffffH\n";
    for (std::size_t k = 0; k < tries; k++) {
        const MsTryRegion &r = msTries()[k];
        o += "  DD imagerel " + masm_.labelName(r.begin) + "\n";
        o += "  DD " + std::to_string(2 * k) + "\n";
        o += "  DD imagerel " + masm_.labelName(r.end) + "\n";
        o += "  DD 0ffffffffH\n";
    }
    for (std::size_t k = 0; k < tries; k++) {
        const MsTryRegion &r = msTries()[k];
        for (std::size_t i = 0; i < r.handlers.size(); i++) {
            o += "  DD imagerel " + r.handlers[i].funclet + "\n";
            o += "  DD " + std::to_string(2 * k + 1) + "\n";
        }
    }
    o += ".xdata ENDS\n\n.CODE\n";
    out_ += o;
}

void MasmCodeGen::emitThrowInfo(const Program &program) {
    if (program.thrown.empty()) return;
    std::string &o = out_;
    o += "\nEXTRN ??_7type_info@@6B@:QWORD\n";
    for (std::size_t i = 0; i < program.thrown.size(); i++) {
        const Type *t = program.thrown[i];
        MicrosoftThrow n;
        std::string why;
        if (!microsoftThrowNames(t, t->size(target_), &n, &why)) continue;

        // **Not PUBLIC, and that is the interesting part.** cl puts each of
        // these in a COMDAT so that two objects throwing an int fold into
        // one; MASM has no way to say COMDAT, so a public copy here collides
        // with cl's at the link - measured, LNK2005 on ??_R0H@8. Keeping them
        // file-local works because the runtime matches a type descriptor by
        // its *name string* rather than by its address, which is the same
        // rule that lets a throw cross a DLL boundary at all.
        o += ".data$r SEGMENT READONLY ALIGN(8) 'DATA'\n";
        // **cl's listing writes `FLAT:` here and ml64 rejects it.** That
        // prefix is 32-bit MASM's way of naming a flat-model address; the
        // 64-bit assembler has no such keyword, so the listing is a record of
        // what cl *means* rather than something that assembles as it stands.
        o += n.descriptor + " DQ ??_7type_info@@6B@\n";
        o += "  DQ 0\n";
        o += "  DB '" + n.decorated + "', 00H\n";
        o += ".data$r ENDS\n";

        o += ".xdata$x SEGMENT READONLY ALIGN(8) 'DATA'\n";
        o += n.catchable + " DD 01H\n";
        o += "  DD imagerel " + n.descriptor + "\n";
        o += "  DD 00H\n";
        o += "  DD 0ffffffffH\n";
        o += "  ORG $+4\n";
        o += "  DD 0" + std::to_string(n.size) + "H\n";
        o += "  DD 00H\n";
        o += n.array + " DD 01H\n";
        o += "  DD imagerel " + n.catchable + "\n";
        o += n.info + " DD 00H\n";
        o += "  DD 00H\n";
        o += "  DD 00H\n";
        o += "  DD imagerel " + n.array + "\n";
        o += ".xdata$x ENDS\n";
    }
}

// The ThrowInfo names are defined in this file, so the spelling must not put
// them in the EXTERN list it writes for everything a call mentions.
void MasmCodeGen::run(const Program &program) {
    std::vector<std::string> mine;
    for (std::size_t i = 0; i < program.thrown.size(); i++) {
        MicrosoftThrow n;
        std::string why;
        if (!microsoftThrowNames(program.thrown[i],
                                 program.thrown[i]->size(target_), &n, &why))
            continue;
        mine.push_back(n.info);
    }
    masm_.predefine(mine);
    // **Before the code, not after it.** The base run() flushes what it has
    // built to the sink when it finishes, so anything appended afterwards is
    // written to a buffer nobody reads. MASM makes two passes, so a `lea` of
    // a ThrowInfo defined above it resolves either way round.
    emitThrowInfo(program);
    X86_64Linux::run(program);
}
