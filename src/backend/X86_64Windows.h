#pragma once

#include "Backend.h"

class WindowsX86_64Target final : public Target {
public:
    int sizeOf(Kind) const override;
    int alignOf(Kind) const override;
    bool plainCharIsSigned() const override { return true; }
    Kind sizeType() const override { return Kind::ULongLong; }

    Kind wcharType() const override { return Kind::UShort; }
    bool microsoftNames() const override { return true; }
    const char *name() const override { return "x86_64-windows"; }
};

// **Which assembler this target is written for** arrives as `gnuAsm` at the two calls below, from
// the Driver that read `-masm=` off argv. ml64 cannot mark a section COMDAT, so a mergeable
// definition collides across translation units where clang, writing the same COFF, can mark it.

class X86_64WindowsBackend final : public Backend {
public:
    const char *name() const override { return "x86_64-windows"; }
    const Target &target() const override { return target_; }
    const Abi &abi() const override;
    bool emits() const override { return true; }
    const char *const *identityMacros() const override;
    bool emitsLineTable(bool gnuAsm) const override;
    std::unique_ptr<CodeGen> codegen(std::ostream &sink, bool gnuAsm) const override;
private:
    WindowsX86_64Target target_;
};
