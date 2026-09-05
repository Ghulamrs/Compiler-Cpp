#include "Spelling.h"

#include <ostream>
#include <string>

void GnuSpelling::op(const Op &x) {
    switch (x.kind) {
    case Op::Reg: o_ += x.text; return;
    case Op::Imm:
        o_ += '$';
        if (!x.immNumeric) { o_ += x.text; return; }
        if (x.immNeg) o_ += '-';
        appendNum(o_, x.uimm);
        return;
    case Op::Mem:
        if (x.hasDisp) appendNum(o_, x.disp);
        o_ += '(';
        o_ += x.text;
        o_ += ')';
        return;
    case Op::Rip: o_ += sym(std::string(x.text.p, x.text.n)); o_ += "(%rip)"; return;
    case Op::Ind: o_ += '*'; o_ += x.text; return;
    case Op::Lbl: o_ += sym(std::string(x.text.p, x.text.n)); return;
    }
}

void GnuSpelling::ins(const std::string &m) { o_ += "  "; o_ += m; o_ += '\n'; }

void GnuSpelling::ins(const std::string &m, const Op &a) {
    o_ += "  "; o_ += m; o_ += ' ';
    op(a);
    o_ += '\n';
}

void GnuSpelling::ins(const std::string &m, const Op &a, const Op &b) {
    o_ += "  "; o_ += m; o_ += ' ';
    op(a);
    o_ += ", ";
    op(b);
    o_ += '\n';
}

void GnuSpelling::defLabel(const std::string &l) { o_ += sym(l); o_ += ":\n"; }

// The flag is for COFF, where a mergeable definition needs its section opened
// before the label. ELF and Mach-O say it afterwards, with `.weak`, exactly as
// they did - so this ignores it and the emitted text is unchanged.
void GnuSpelling::functionBegin(const std::string &name, bool exported,
                                bool mergeable) {
    (void)mergeable;
    if (exported) globl(name);
    textSection();
    defLabel(name);
}

// **Unwind data, and it is the same three directives in every function.** A
// cxx1 frame has one shape, so the CFA is rbp + 16 throughout; without it a
// backtrace stops here and no exception passes. MASM has always said this.
void GnuSpelling::prologue(int frameSize, const std::string &lsda) {
    o_ += "  .cfi_startproc\n";
    if (!lsda.empty()) {
        o_ += "  .cfi_personality 155, DW.ref.__gxx_personality_v0\n";
        o_ += "  .cfi_lsda 27, " + lsda + "\n";
    }
    ins("push", reg("%rbp"));
    o_ += "  .cfi_def_cfa_offset 16\n";
    o_ += "  .cfi_offset %rbp, -16\n";
    ins("mov", reg("%rsp"), reg("%rbp"));
    o_ += "  .cfi_def_cfa_register %rbp\n";
    if (frameSize > 0) ins("sub", imm(frameSize), reg("%rsp"));
}

void GnuSpelling::functionEnd(const std::string &) {
    o_ += "  .cfi_endproc\n";
}

void GnuSpelling::globl(const std::string &name) {
    o_ += "  .globl "; o_ += sym(name); o_ += '\n';
}

// Measured from clang: `.weak` beside the `.globl`, which is what makes the
// linker fold the copies of an inline function rather than reject them.
void GnuSpelling::weakDefinition(const std::string &name) {
    o_ += "  .weak "; o_ += sym(name); o_ += '\n';
}

void GnuSpelling::fileEntry(int n, const std::string &name) {
    o_ += "  .file ";
    appendNum(o_, n);
    o_ += " \"";
    o_ += name;
    o_ += "\"\n";
}

void GnuSpelling::location(int file, int line, int column) {
    o_ += "  .loc ";
    appendNum(o_, file);
    o_ += ' ';
    appendNum(o_, line);
    o_ += ' ';
    appendNum(o_, column);
    o_ += '\n';
}

void GnuSpelling::textSection()   { o_ += "  .text\n"; }
void GnuSpelling::rodataSection() { o_ += "  .section .rodata\n"; }
void GnuSpelling::dataSection()   { o_ += "  .data\n"; }
void GnuSpelling::bssSection()    { o_ += "  .bss\n"; }

void GnuSpelling::objectType(const std::string &name) {
    o_ += "  .type "; o_ += sym(name); o_ += ", @object\n";
}

void GnuSpelling::objectSize(const std::string &name, int size) {
    o_ += "  .size "; o_ += sym(name); o_ += ", "; appendNum(o_, size); o_ += '\n';
}

void GnuSpelling::align(int n) { o_ += "  .align "; appendNum(o_, n); o_ += '\n'; }
void GnuSpelling::zero(int n)  { o_ += "  .zero ";  appendNum(o_, n); o_ += '\n'; }

void GnuSpelling::dataInt(int size, long long v) {
    switch (size) {
    case 1: o_ += "  .byte "; break;
    case 2: o_ += "  .word "; break;
    case 4: o_ += "  .long "; break;
    default: o_ += "  .quad "; break;
    }
    appendNum(o_, v);
    o_ += '\n';
}

void GnuSpelling::dataSym(const std::string &s, long long off) {
    o_ += "  .quad ";
    o_ += sym(s);
    if (off > 0) { o_ += '+'; appendNum(o_, off); }
    else if (off < 0) { o_ += '-'; appendNum(o_, -off); }
    o_ += '\n';
}

void GnuSpelling::dataBytes(const std::string &bytes) {
    o_ += "  .byte ";
    for (std::size_t k = 0; k < bytes.size(); k++) {
        if (k) o_ += ", ";
        appendNum(o_, static_cast<long long>(
                          static_cast<unsigned char>(bytes[k])));
    }
    o_ += '\n';
}


// --- CoffSpelling -----------------------------------------------------------

// Quoted where GNU-as would not take the name as an identifier. A Microsoft
// mangled name always carries a '?' or an '@'; a C name carries neither, so
// `printf` and `__CxxFrameHandler3` are written plainly, as clang writes them.
std::string CoffSpelling::sym(const std::string &name) const {
    if (name.find('?') == std::string::npos &&
        name.find('@') == std::string::npos)
        return name;
    return "\"" + name + "\"";
}

void CoffSpelling::functionBegin(const std::string &name, bool exported,
                                 bool mergeable) {
    if (mergeable) {
        // The section carries the COMDAT bit and names the symbol it folds on;
        // `discard` is IMAGE_COMDAT_SELECT_ANY, which is what an inline
        // definition wants - keep one, drop the rest.
        o_ += "  .section .text,\"xr\",discard," + sym(name) + "\n";
        opened_ = name;
    } else {
        textSection();
    }
    if (exported) globl(name);
    defLabel(name);
}

// For a global the code generator says this *before* the label, which is where
// the section directive has to go; for a function functionBegin has already
// opened one, and a second would start an empty section.
void CoffSpelling::weakDefinition(const std::string &name) {
    if (opened_ == name) { opened_.clear(); return; }
    o_ += "  .section .rdata,\"dr\",discard," + sym(name) + "\n";
}

// COFF spells the read-only segment .rdata, and has no .type or .size.
void CoffSpelling::rodataSection() { o_ += "  .section .rdata,\"dr\"\n"; }
void CoffSpelling::objectType(const std::string &name) { (void)name; }
void CoffSpelling::objectSize(const std::string &name, int size) {
    (void)name; (void)size;
}
