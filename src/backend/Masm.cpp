#include "Masm.h"

#include "../Mangle.h"

#include <cstdio>
#include <cstdlib>
#include <ostream>
#include <string>
#include <vector>

namespace {

[[noreturn]] void give_up(const std::string &what, const std::string &why) {
    std::fprintf(stderr, "cxx1: masm: %s\n  for: %s\n", why.c_str(),
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
        // **The whole frame moved, so every offset into it moves with it.** This
        // target takes rbp after the allocation, exactly frameSize lower, so every
        // `[rbp + d]` becomes `[rbp + d + frameSize]` - the positive ones included.
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

// `mergeable` is what a COMDAT would be told, and ml64 has no directive that
// reaches the COMDAT bit - see CoffSpelling, which exists for that reason.
void MasmSpelling::functionBegin(const std::string &name, bool exported,
                                 bool mergeable) {
    (void)mergeable;
    defined_.insert(name);
    if (exported) exported_.insert(name);
    flushPending();
    if (seg_ != Code) { o_ += "\n.CODE\n"; seg_ = Code; }

    // **`PROC`, not `PROC FRAME`, and the unwind data written by hand.** No MASM
    // directive reaches the handler *data* inside UNWIND_INFO, and cl writes
    // $pdata$ and $unwind$ itself. Done for every function, so one path is walked.
    fnName_ = name;
    o_ += mangle(name); o_ += " PROC\n";
    o_ += "$LNbeg$"; o_ += mangle(name); o_ += ":\n";
}

void MasmSpelling::raw(const std::string &text) {
    flushPending();
    o_ += text;
}

void MasmSpelling::prologue(int frameSize, const std::string &lsda) {
    // The caller passes a non-empty name when the function has handlers. What it
    // *is* does not matter here - an Itanium table label means nothing to MASM -
    // only that there is one, which sets the flags and whether a FuncInfo follows.
    hasEh_ = !lsda.empty();
    frameSize_ = frameSize;
    const std::string m = mangle(fnName_);

    // **The frame pointer is taken *after* the allocation on this target**, which
    // is the whole difference from the Itanium prologue: every displacement in an
    // FH3 table is an unsigned offset up from the establisher. Measured against cl.
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

// **Every offset here is a label difference, not a counted byte.** An unwind code
// records where its instruction *ends*, and `sub rsp, 40` is four bytes where
// `sub rsp, 400` is seven - the assembler chooses, so the assembler is asked.
void MasmSpelling::functionEnd(const std::string &name) {
    const std::string m = mangle(name);
    // **The dot is the whole of it.** A segment called `pdata` is a segment called
    // pdata; the linker builds the image's exception directory from `.pdata`, and
    // without it the runtime finds no unwind record - measured with dumpbin.
    o_ += "$LNend$" + m + ":\n";
    o_ += m + " ENDP\n";

    // READONLY and the alignment, or the linker finds two .pdata
    // sections with different attributes and says so.
    o_ += "\n.pdata SEGMENT READONLY ALIGN(4) 'DATA'\n";
    o_ += "$pdata$" + m + " DD imagerel $LNbeg$" + m + "\n";
    o_ += "  DD imagerel $LNend$" + m + "\n";
    o_ += "  DD imagerel $unwind$" + m + "\n";
    o_ += ".pdata ENDS\n";

    // UNWIND_INFO: version 1 with no flags, the prologue's size, how many codes
    // follow, and the frame register - rbp, at offset 0 from where rsp stood when
    // it was set. The codes are last-first, the order an unwinder undoes them in.
    o_ += ".xdata SEGMENT READONLY ALIGN(8) 'DATA'\n";
    // Version 1, and the flags in the top five bits. 0x19 is UNW_FLAG_EHANDLER and
    // UNW_FLAG_UHANDLER, which is what cl writes for a function with a `try`.
    // Without them the runtime unwinds past the frame and never reads the FuncInfo.
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
    // **A symbol named by data is a use, and MASM needs it declared.** The two
    // operand paths record one; this did not, so a vtable slot holding a
    // function defined in another translation unit reached ml64 undeclared -
    // `error A2006: undefined symbol`, on ten of Compiler++'s sixteen and on
    // nothing in the suite, where every class is defined where it is used.
    // The mirror of A-02: there a vtable slot was a use nothing emitted, here
    // it is a use nothing declared.
    referenced_.insert(sym);
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
    sink << "; Generated by cxx1 for x86_64-windows, in MASM syntax for ml64.\n";
    sink << "; The instruction selection is the same one the Linux backend makes;\n";
    sink << "; only the spelling is Microsoft's. See src/backend/Masm.cpp.\n\n";

    // **DOTNAME lets a segment be called `.pdata`, and NOSCOPED lets the unwind
    // data name the labels inside a procedure.** Both are needed for one reason:
    // the tables live outside the function and measure into it.
    sink << "OPTION DOTNAME\n";
    sink << "OPTION NOSCOPED\n";
    // **MASM exports every PROC unless told otherwise**, so a `static` function
    // left out of the PUBLIC list below came out of the object External anyway.
    // Invisible to mangled-names: the name was right, the storage class was not.
    sink << "OPTION PROC:PRIVATE\n\n";

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
    sink << trailer_;
    trailer_.clear();
    sink << "\nEND\n";
}

// **The chain the Microsoft ABI wants before anything may be thrown** - ??_R0H@8,
// _CT??_R0H@84, _CTA1H, _TI1H - measured from cl's listing, `imagerel` throughout
// and in cl's segments. Then -2, the scratch word saying nothing is entered yet.
int MasmCodeGen::establisherOffset(int slot) const {
    return masm_.frameSize_ - slot;
}

// **Through the assembler rather than as a raw string**, so the renderer's
// `+ frameSize_` is the only place the frame moves: `[rbp-slot]` written here
// comes out as `[rbp + frameSize - slot]` there, which is the same arithmetic
// establisherOffset does for the tables.
void MasmCodeGen::storeUnwindHelp(int slot) {
    a_->ins("movq", imm(-2), mem(-slot, "%rbp"));
}

// **A funclet is a slice of the ordinary output, lifted.** Walking the handler
// appends its code like any other, so remembering where that began and cutting
// back to it gives the body exactly - and the code generator knows none of it.
std::string MasmCodeGen::beginFunclet() {
    masm_.raw("");                       // nothing pending inside the slice
    funcletMark_ = out_.size();
    funcletSymbol_ = masm_.mangledName() + funcletKind_ +
                     std::to_string(funcletIndex_++);
    return funcletSymbol_;
}

// The funclet's own frame: `rdx` is the establisher frame, so adding the parent's
// frame size back recovers the rbp every local was written against, and a funclet
// *returns* where to carry on, in rax. A cleanup has nothing to return to.
void MasmCodeGen::endCleanupFunclet() {
    closeFunclet(std::string());
    funcletKind_ = "$catch$";
}

void MasmCodeGen::endFunclet(const std::string &resume) {
    closeFunclet("  lea rax, " + masm_.labelName(resume) + "\n");
}

void MasmCodeGen::closeFunclet(const std::string &tail) {
    masm_.raw("");
    std::string body = out_.substr(funcletMark_);
    out_.resize(funcletMark_);

    const std::string sym = funcletSymbol_;
    std::string f;
    // **`.text$x`, and the dot is the whole of it** - the same trap as `.pdata`. A
    // segment called `text` gets data attributes, so the handler faults at its own
    // first instruction; 'CODE' is what gives it execute permission beside .text.
    f += "\n.text$x SEGMENT ALIGN(16) 'CODE'\n";
    f += sym + " PROC\n";
    f += "$LNbeg$" + sym + ":\n";
    f += "  mov QWORD PTR [rsp+16], rdx\n";
    f += "  push rbp\n";
    f += "$LNpush$" + sym + ":\n";
    f += "  sub rsp, 32\n";
    f += "$LNprolog$" + sym + ":\n";
    // **rdx is the establisher frame, and that is now exactly the parent's rbp** -
    // the two became one thing when the frame pointer moved to the bottom of the
    // allocation, so the handler reaches the parent's locals with no adjustment.
    f += "  mov rbp, rdx\n";
    f += body;
    f += tail;
    f += "  add rsp, 32\n";
    f += "  pop rbp\n";
    f += "  ret 0\n";
    f += "$LNend$" + sym + ":\n";
    f += sym + " ENDP\n";
    f += ".text$x ENDS\n";

    // A funclet carries unwind data of its own, naming the same handler and *the
    // parent's* FuncInfo - the two share one description of the try. Its .pdata
    // goes to a pile written after every function, for the sorting reason recorded.
    masm_.trailer_ += "\n.pdata SEGMENT READONLY ALIGN(4) 'DATA'\n";
    masm_.trailer_ += "$pdata$" + sym + " DD imagerel $LNbeg$" + sym + "\n";
    masm_.trailer_ += "  DD imagerel $LNend$" + sym + "\n";
    masm_.trailer_ += "  DD imagerel $unwind$" + sym + "\n";
    masm_.trailer_ += ".pdata ENDS\n";
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

// The four FH3 tables, written after the function they describe. **Every offset is
// measured from the establisher frame**, so a slot at [rbp-N] is at frameSize-N.
// **And a cleanup is a state, not a try block**: its action is the funclet.
void MasmCodeGen::emitCleanupTables(const Function &fn) {
    (void)fn;
    const std::string m = masm_.mangledName();
    const std::size_t states = msTries().size();

    std::string o;
    o += funclets_;
    funclets_.clear();
    funcletIndex_ = 0;

    o += "\n.xdata SEGMENT READONLY ALIGN(8) 'DATA'\n";
    o += "$cppxdata$" + m + " DD 019930522H\n";
    o += "  DD " + std::to_string(states) + "\n";
    o += "  DD imagerel $stateUnwindMap$" + m + "\n";
    o += "  DD 00H\n";                      // no try blocks
    o += "  DD 00H\n";                      // and so no try map
    o += "  DD " + std::to_string(states + 1) + "\n";
    o += "  DD imagerel $ip2state$" + m + "\n";
    o += "  DD " + std::to_string(establisherOffset(msTries()[0].unwindHelpSlot)) + "\n";
    o += "  DD 00H\n";
    o += "  DD 01H\n";

    o += "$stateUnwindMap$" + m;
    for (std::size_t k = 0; k < states; k++) {
        o += (k == 0 ? " DD " : "  DD ");
        // toState: the region before this one, and -1 for the first, which is
        // what says "nothing further in this frame".
        o += (k == 0 ? std::string("0ffffffffH") : std::to_string(k - 1)) + "\n";
        o += "  DD imagerel " + msTries()[k].cleanupFunclet + "\n";
    }

    o += "$ip2state$" + m + " DD imagerel $LNbeg$" + m + "\n";
    o += "  DD 0ffffffffH\n";
    for (std::size_t k = 0; k < states; k++) {
        o += "  DD imagerel " + masm_.labelName(msTries()[k].begin) + "\n";
        o += "  DD " + std::to_string(k) + "\n";
    }
    o += ".xdata ENDS\n\n.CODE\n";
    out_ += o;
}

void MasmCodeGen::emitExceptionTables(const Function &fn) {
    if (msTries().empty()) {
        if (!callSites().empty()) emitLsda(fn.symbol());
        out_ += funclets_;
        funclets_.clear();
        funcletIndex_ = 0;
        return;
    }

    const std::string m = masm_.mangledName();

    // **Cleanups and handlers never share a function**, which the parser enforces
    // on every target: a local with a destructor and a `try` in one function is
    // refused, each being a range that would have to split the other.
    if (msTries()[0].isCleanup) { emitCleanupTables(fn); return; }

    const std::size_t tries = msTries().size();
    std::string o;
    o += funclets_;
    funclets_.clear();
    funcletIndex_ = 0;

    // **Two states per try**: the body is one and its handlers the next, so try k
    // owns states 2k and 2k+1. Numbered rather than nested because the parser
    // refuses a `try` inside another, which is what tryLow and tryHigh are for.
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
    o += "  DD " + std::to_string(establisherOffset(msTries()[0].unwindHelpSlot)) + "\n";
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
                                              ? 0 : establisherOffset(h.objectSlot)) + "\n";
            o += "  DD imagerel " + h.funclet + "\n";
            // The frame size itself, not an offset into it: what the runtime
            // adds to the establisher to reach the handler's own frame.
            o += "  DD " + std::to_string(masm_.frameSize_) + "\n";
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

// **The five objects the Microsoft ABI wants before it will answer a
// `dynamic_cast`**, measured from clang. Itanium hangs two off the back of a
// vtable; this hangs five in front of one:
//
//   ??_R0  the type descriptor - the type_info vfptr, a spare word, and the
//          decorated name, which is the string the runtime actually compares
//   ??_R1  where one class sits inside another: how many bases it contains,
//          then mdisp/pdisp/vdisp and the attributes
//   ??_R2  the array of those, this class first and then up the chain
//   ??_R3  the hierarchy over that array, carrying its length
//   ??_R4  the complete-object locator, which names the first and the fourth
//          and sits one word in front of the vftable
//
// **Not PUBLIC, for the reason the throw chain records**: cl puts each in a
// COMDAT, MASM cannot say COMDAT, and a public copy collides with cl's. The
// runtime matches on the descriptor's name string, so file-local is enough.
void MasmCodeGen::emitClassRtti(const Program &program) {
    if (program.rtti.empty()) return;
    std::string &o = out_;

    // The chain of every class named, closed and deduplicated: a class's array
    // lists its bases' descriptors, so a base needs the full set of records
    // even when nothing in this file names it.
    std::vector<const Type *> all;
    for (std::size_t i = 0; i < program.rtti.size(); i++)
        for (const Type *k = program.rtti[i]; k != nullptr; k = k->base()) {
            bool had = false;
            for (std::size_t j = 0; j < all.size(); j++) if (all[j] == k) had = true;
            if (!had) all.push_back(k);
        }

    for (std::size_t i = 0; i < all.size(); i++) {
        MicrosoftRtti n;
        std::string why;
        if (!microsoftClassRttiNames(all[i], &n, &why)) continue;

        int contained = 0;
        for (const Type *k = all[i]->base(); k != nullptr; k = k->base())
            contained++;

        o += ".data$r SEGMENT READONLY ALIGN(8) 'DATA'\n";
        o += n.descriptor + " DQ ??_7type_info@@6B@\n";
        o += "  DQ 0\n";
        o += "  DB '" + n.decorated + "', 00H\n";

        // Where this class sits inside itself: at the top, never virtual. Those
        // four numbers are constant because a class with a second base is
        // refused - its first base is always at offset zero.
        o += n.baseDescriptor + " DD imagerel " + n.descriptor + "\n";
        o += "  DD 0" + std::to_string(contained) + "H\n";
        o += "  DD 00H\n";              // mdisp
        o += "  DD 0ffffffffH\n";       // pdisp - -1, there is no vbtable
        o += "  DD 00H\n";              // vdisp
        o += "  DD 040H\n";             // attributes
        o += "  DD imagerel " + n.hierarchy + "\n";

        o += n.array + " DD imagerel " + n.baseDescriptor + "\n";
        for (const Type *k = all[i]->base(); k != nullptr; k = k->base()) {
            MicrosoftRtti b;
            if (!microsoftClassRttiNames(k, &b, &why)) break;
            o += "  DD imagerel " + b.baseDescriptor + "\n";
        }
        o += "  DD 00H\n";

        o += n.hierarchy + " DD 00H\n";
        o += "  DD 00H\n";              // attributes - no MI, no virtual bases
        o += "  DD 0" + std::to_string(contained + 1) + "H\n";
        o += "  DD imagerel " + n.array + "\n";

        // The locator names itself, which is how the runtime recovers the image
        // base every other field is relative to.
        o += n.locator + " DD 01H\n";
        o += "  DD 00H\n";              // the vfptr's offset in the object
        o += "  DD 00H\n";              // cdOffset
        o += "  DD imagerel " + n.descriptor + "\n";
        o += "  DD imagerel " + n.hierarchy + "\n";
        o += "  DD imagerel " + n.locator + "\n";
        o += ".data$r ENDS\n";
    }
}

void MasmCodeGen::emitThrowInfo(const Program &program) {
    if (program.thrown.empty()) return;
    std::string &o = out_;
    for (std::size_t i = 0; i < program.thrown.size(); i++) {
        const Type *t = program.thrown[i];
        MicrosoftThrow n;
        std::string why;
        if (!microsoftThrowNames(t, t->size(target_), &n, &why)) continue;

        // **Not PUBLIC, and that is the interesting part.** cl puts each in a
        // COMDAT and MASM cannot say COMDAT, so a public copy collides with cl's -
        // measured, LNK2005. File-local works: the runtime matches by name string.
        o += ".data$r SEGMENT READONLY ALIGN(8) 'DATA'\n";
        // **cl's listing writes `FLAT:` here and ml64 rejects it.** That prefix is
        // 32-bit MASM's way of naming a flat-model address; the 64-bit assembler
        // has no such keyword, so the listing records what cl means.
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
    // **And every class descriptor this file defines**, for the same reason:
    // the spelling writes an EXTERN for each name a call mentions, and these
    // are mentioned by the `lea` in front of every __RTDynamicCast while being
    // laid down a few lines above it. The chain is walked because a base's
    // descriptor is defined here too even when nothing names it.
    for (std::size_t i = 0; i < program.rtti.size(); i++)
        for (const Type *k = program.rtti[i]; k != nullptr; k = k->base()) {
            MicrosoftRtti n;
            std::string why;
            if (!microsoftClassRttiNames(k, &n, &why)) break;
            // **All five, not the descriptor alone.** The descriptor is the one
            // a `lea` mentions, so it was the only one that had to be here while
            // only instructions recorded a reference. Data records one now, and
            // a vtable's first word is the locator - so the locator, the
            // hierarchy, the base array and the base descriptor are named by
            // data laid down in this very file and would otherwise be declared
            // EXTERN and defined, which ml64 calls a symbol redefinition.
            mine.push_back(n.descriptor);
            mine.push_back(n.baseDescriptor);
            mine.push_back(n.array);
            mine.push_back(n.hierarchy);
            mine.push_back(n.locator);
        }
    masm_.predefine(mine);
    // **Before the code, not after it.** The base run() flushes what it has built
    // when it finishes, so anything appended afterwards goes to a buffer nobody
    // reads. MASM makes two passes, so a `lea` of a ThrowInfo above resolves.
    // `??_7type_info@@6B@` heads every type descriptor, thrown or cast alike,
    // and ml64 wants it declared once however many ask for it.
    if (!program.thrown.empty() || !program.rtti.empty())
        out_ += "\nEXTRN ??_7type_info@@6B@:QWORD\n";
    // **A pure virtual's slot names a routine no object file here defines.**
    // GNU as takes an undeclared symbol and leaves it to the linker; ml64 will
    // not, and answers `A2006: undefined symbol : _purecall`. Declared once
    // however many slots hold it, and only when one does.
    for (std::size_t g = 0; g < program.globals.size(); g++) {
        bool found = false;
        const std::vector<GlobalPiece> &pieces = program.globals[g].init;
        for (std::size_t i = 0; i < pieces.size(); i++)
            if (pieces[i].symbol == "_purecall") { found = true; break; }
        if (found) { out_ += "\nEXTRN _purecall:PROC\n"; break; }
    }
    emitClassRtti(program);
    emitThrowInfo(program);
    X86_64Linux::run(program);
}
