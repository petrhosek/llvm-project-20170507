//===- NativeTypeFunctionSig.cpp - info about function signature -*- C++-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/Native/NativeTypeFunctionSig.h"

#include "llvm/DebugInfo/CodeView/LazyRandomTypeCollection.h"
#include "llvm/DebugInfo/PDB/Native/NativeEnumTypes.h"
#include "llvm/DebugInfo/PDB/PDBExtras.h"
#include "llvm/DebugInfo/PDB/Native/PDBFile.h"
#include "llvm/DebugInfo/PDB/Native/TpiStream.h"

using namespace llvm;
using namespace llvm::codeview;
using namespace llvm::pdb;

namespace {
// This is kind of a silly class, hence why we keep it private to the file.
// It's only purpose is to wrap the real type record.  I guess this is so that
// we can have the lexical parent point to the function instead of the global
// scope.
class NativeTypeFunctionArg : public NativeRawSymbol {
public:
  NativeTypeFunctionArg(NativeSession &Session,
                        std::unique_ptr<PDBSymbol> RealType)
      : NativeRawSymbol(Session, PDB_SymType::FunctionArg, 0),
        RealType(std::move(RealType)) {}

  void dump(raw_ostream &OS, int Indent, PdbSymbolIdField ShowIdFields,
            PdbSymbolIdField RecurseIdFields) const override {
    NativeRawSymbol::dump(OS, Indent, ShowIdFields, RecurseIdFields);

    dumpSymbolIdField(OS, "typeId", getTypeId(), Indent, Session,
                      PdbSymbolIdField::Type, ShowIdFields, RecurseIdFields);
  }

  SymIndexId getTypeId() const override { return RealType->getSymIndexId(); }

  std::unique_ptr<PDBSymbol> RealType;
};

class NativeEnumFunctionArgs : public IPDBEnumChildren<PDBSymbol> {
public:
  NativeEnumFunctionArgs(NativeSession &Session,
                         std::unique_ptr<NativeEnumTypes> TypeEnumerator)
      : Session(Session), TypeEnumerator(std::move(TypeEnumerator)) {}

  uint32_t getChildCount() const override {
    return TypeEnumerator->getChildCount();
  }
  std::unique_ptr<PDBSymbol> getChildAtIndex(uint32_t Index) const override {
    return wrap(TypeEnumerator->getChildAtIndex(Index));
  }
  std::unique_ptr<PDBSymbol> getNext() override {
    return wrap(TypeEnumerator->getNext());
  }

  void reset() override { TypeEnumerator->reset(); }

private:
  std::unique_ptr<PDBSymbol> wrap(std::unique_ptr<PDBSymbol> S) const {
    if (!S)
      return nullptr;
    auto NTFA = llvm::make_unique<NativeTypeFunctionArg>(Session, std::move(S));
    return PDBSymbol::create(Session, std::move(NTFA));
  }
  NativeSession &Session;
  std::unique_ptr<NativeEnumTypes> TypeEnumerator;
};
} // namespace

NativeTypeFunctionSig::NativeTypeFunctionSig(NativeSession &Session,
                                             SymIndexId Id,
                                             codeview::TypeIndex Index,
                                             codeview::ProcedureRecord Proc)
    : NativeRawSymbol(Session, PDB_SymType::FunctionSig, Id),
      Proc(std::move(Proc)), Index(Index), IsMemberFunction(false) {}

NativeTypeFunctionSig::NativeTypeFunctionSig(
    NativeSession &Session, SymIndexId Id, codeview::TypeIndex Index,
    codeview::MemberFunctionRecord MemberFunc)
    : NativeRawSymbol(Session, PDB_SymType::FunctionSig, Id),
      MemberFunc(std::move(MemberFunc)), Index(Index), IsMemberFunction(true) {}

void NativeTypeFunctionSig::initialize() {
  if (IsMemberFunction) {
    ClassParentId =
        Session.getSymbolCache().findSymbolByTypeIndex(MemberFunc.ClassType);
    initializeArgList(MemberFunc.ArgumentList);
  } else {
    initializeArgList(Proc.ArgumentList);
  }
}

NativeTypeFunctionSig::~NativeTypeFunctionSig() {}

void NativeTypeFunctionSig::initializeArgList(codeview::TypeIndex ArgListTI) {
  TpiStream &Tpi = cantFail(Session.getPDBFile().getPDBTpiStream());
  CVType CVT = Tpi.typeCollection().getType(ArgListTI);

  cantFail(TypeDeserializer::deserializeAs<ArgListRecord>(CVT, ArgList));
}

void NativeTypeFunctionSig::dump(raw_ostream &OS, int Indent,
                                 PdbSymbolIdField ShowIdFields,
                                 PdbSymbolIdField RecurseIdFields) const {

  NativeRawSymbol::dump(OS, Indent, ShowIdFields, RecurseIdFields);

  dumpSymbolIdField(OS, "lexicalParentId", 0, Indent, Session,
                    PdbSymbolIdField::LexicalParent, ShowIdFields,
                    RecurseIdFields);

  dumpSymbolField(OS, "callingConvention", getCallingConvention(), Indent);
  dumpSymbolField(OS, "count", getCount(), Indent);
  dumpSymbolIdField(OS, "typeId", getTypeId(), Indent, Session,
                    PdbSymbolIdField::Type, ShowIdFields, RecurseIdFields);
  if (IsMemberFunction)
    dumpSymbolField(OS, "thisAdjust", getThisAdjust(), Indent);
  dumpSymbolField(OS, "constructor", hasConstructor(), Indent);
  dumpSymbolField(OS, "constType", isConstType(), Indent);
  dumpSymbolField(OS, "isConstructorVirtualBase", isConstructorVirtualBase(),
                  Indent);
  dumpSymbolField(OS, "isCxxReturnUdt", isCxxReturnUdt(), Indent);
  dumpSymbolField(OS, "unalignedType", isUnalignedType(), Indent);
  dumpSymbolField(OS, "volatileType", isVolatileType(), Indent);
}

std::unique_ptr<IPDBEnumSymbols>
NativeTypeFunctionSig::findChildren(PDB_SymType Type) const {
  if (Type != PDB_SymType::FunctionArg)
    return llvm::make_unique<NullEnumerator<PDBSymbol>>();

  auto NET = llvm::make_unique<NativeEnumTypes>(Session,
                                                /* copy */ ArgList.ArgIndices);
  return std::unique_ptr<IPDBEnumSymbols>(
      new NativeEnumFunctionArgs(Session, std::move(NET)));
}

SymIndexId NativeTypeFunctionSig::getClassParentId() const {
  if (!IsMemberFunction)
    return 0;

  return ClassParentId;
}

PDB_CallingConv NativeTypeFunctionSig::getCallingConvention() const {
  return IsMemberFunction ? MemberFunc.CallConv : Proc.CallConv;
}

uint32_t NativeTypeFunctionSig::getCount() const {
  return IsMemberFunction ? (1 + MemberFunc.getParameterCount())
                          : Proc.getParameterCount();
}

SymIndexId NativeTypeFunctionSig::getTypeId() const {
  TypeIndex ReturnTI =
      IsMemberFunction ? MemberFunc.getReturnType() : Proc.getReturnType();

  return Session.getSymbolCache().findSymbolByTypeIndex(ReturnTI);
}

int32_t NativeTypeFunctionSig::getThisAdjust() const {
  return IsMemberFunction ? MemberFunc.getThisPointerAdjustment() : 0;
}

bool NativeTypeFunctionSig::hasConstructor() const {
  if (!IsMemberFunction)
    return false;

  return (MemberFunc.getOptions() & FunctionOptions::Constructor) !=
         FunctionOptions::None;
}

bool NativeTypeFunctionSig::isConstType() const { return false; }

bool NativeTypeFunctionSig::isConstructorVirtualBase() const {
  if (!IsMemberFunction)
    return false;

  return (MemberFunc.getOptions() &
          FunctionOptions::ConstructorWithVirtualBases) !=
         FunctionOptions::None;
}

bool NativeTypeFunctionSig::isCxxReturnUdt() const {
  FunctionOptions Options =
      IsMemberFunction ? MemberFunc.getOptions() : Proc.getOptions();
  return (Options & FunctionOptions::CxxReturnUdt) != FunctionOptions::None;
}

bool NativeTypeFunctionSig::isUnalignedType() const { return false; }

bool NativeTypeFunctionSig::isVolatileType() const { return false; }
