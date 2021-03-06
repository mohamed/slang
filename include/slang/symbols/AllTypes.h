//------------------------------------------------------------------------------
//! @file AllTypes.h
//! @brief All type symbol definitions
//
// File is under the MIT license; see LICENSE for details
//------------------------------------------------------------------------------
#pragma once

#include "slang/symbols/Scope.h"
#include "slang/symbols/Type.h"

namespace slang {

class Compilation;
class SubroutineSymbol;

struct IntegerTypeSyntax;

/// A base class for integral types, which include all scalar types, predefined integer types,
/// packed arrays, packed structures, packed unions, and enum types.
class IntegralType : public Type {
public:
    /// The total width of the type in bits.
    bitwidth_t bitWidth;

    /// Indicates whether or not the integer participates in signed arithmetic.
    bool isSigned;

    /// Indicates whether the integer is composed of 4-state bits or 2-state bits.
    bool isFourState;

    /// If this is a simple bit vector type, returns the address range of
    /// the bits in the vector. Otherwise the behavior is undefined (will assert).
    ConstantRange getBitVectorRange() const;

    /// Indicates whether the underlying type was declared using the 'reg' keyword.
    bool isDeclaredReg() const;

    static const Type& fromSyntax(Compilation& compilation, const IntegerTypeSyntax& syntax,
                                  LookupLocation location, const Scope& scope, bool forceSigned);

    static const Type& fromSyntax(Compilation& compilation, SyntaxKind integerKind,
                                  span<const VariableDimensionSyntax* const> dimensions,
                                  bool isSigned, LookupLocation location, const Scope& scope);

    ConstantValue getDefaultValueImpl() const;

    static bool isKind(SymbolKind kind);

protected:
    IntegralType(SymbolKind kind, string_view name, SourceLocation loc, bitwidth_t bitWidth,
                 bool isSigned, bool isFourState);
};

/// Represents the single-bit scalar types.
class ScalarType : public IntegralType {
public:
    enum Kind { Bit, Logic, Reg } scalarKind;

    ScalarType(Kind scalarKind);
    ScalarType(Kind scalarKind, bool isSigned);

    static bool isKind(SymbolKind kind) { return kind == SymbolKind::ScalarType; }
};

/// Represents the predefined integer types, which are essentially predefined vector types.
class PredefinedIntegerType : public IntegralType {
public:
    enum Kind { ShortInt, Int, LongInt, Byte, Integer, Time } integerKind;

    PredefinedIntegerType(Kind integerKind);
    PredefinedIntegerType(Kind integerKind, bool isSigned);

    static bool isDefaultSigned(Kind integerKind);

    static bool isKind(SymbolKind kind) { return kind == SymbolKind::PredefinedIntegerType; }
};

/// Represents one of the predefined floating point types, which are used for representing real
/// numbers.
class FloatingType : public Type {
public:
    enum Kind { Real, ShortReal, RealTime } floatKind;

    explicit FloatingType(Kind floatKind);

    ConstantValue getDefaultValueImpl() const;

    static bool isKind(SymbolKind kind) { return kind == SymbolKind::FloatingType; }
};

class EnumValueSymbol;
struct EnumTypeSyntax;

/// Represents an enumerated type.
class EnumType : public IntegralType, public Scope {
public:
    const Type& baseType;

    EnumType(Compilation& compilation, SourceLocation loc, const Type& baseType,
             LookupLocation lookupLocation);

    static const Type& fromSyntax(Compilation& compilation, const EnumTypeSyntax& syntax,
                                  LookupLocation location, const Scope& scope, bool forceSigned);
    static bool isKind(SymbolKind kind) { return kind == SymbolKind::EnumType; }

    iterator_range<specific_symbol_iterator<EnumValueSymbol>> values() const {
        return membersOfType<EnumValueSymbol>();
    }
};

/// Represents an enumerated value / member.
class EnumValueSymbol : public ValueSymbol {
public:
    EnumValueSymbol(string_view name, SourceLocation loc);

    const ConstantValue& getValue() const;
    void setValue(ConstantValue value);

    void serializeTo(ASTSerializer& serializer) const;

    static EnumValueSymbol& fromSyntax(Compilation& compilation, const DeclaratorSyntax& syntax,
                                       const Type& type, optional<int32_t> index);

    static bool isKind(SymbolKind kind) { return kind == SymbolKind::EnumValue; }

private:
    const ConstantValue* value = nullptr;
};

/// Represents a packed array of some simple element type (vectors, packed structures, other packed
/// arrays).
class PackedArrayType : public IntegralType {
public:
    const Type& elementType;
    ConstantRange range;

    PackedArrayType(const Type& elementType, ConstantRange range);

    static const Type& fromSyntax(Compilation& compilation, const Type& elementType,
                                  ConstantRange range, const SyntaxNode& syntax);

    static bool isKind(SymbolKind kind) { return kind == SymbolKind::PackedArrayType; }
};

/// Represents an unpacked array of some other type.
class UnpackedArrayType : public Type {
public:
    const Type& elementType;
    ConstantRange range;

    UnpackedArrayType(const Type& elementType, ConstantRange range);

    static const Type& fromSyntax(Compilation& compilation, const Type& elementType,
                                  LookupLocation location, const Scope& scope,
                                  const SyntaxList<VariableDimensionSyntax>& dimensions);

    ConstantValue getDefaultValueImpl() const;

    static bool isKind(SymbolKind kind) { return kind == SymbolKind::UnpackedArrayType; }
};

struct StructUnionTypeSyntax;

/// Represents a packed structure of members.
class PackedStructType : public IntegralType, public Scope {
public:
    PackedStructType(Compilation& compilation, bitwidth_t bitWidth, bool isSigned,
                     bool isFourState);

    static const Type& fromSyntax(Compilation& compilation, const StructUnionTypeSyntax& syntax,
                                  LookupLocation location, const Scope& scope, bool forceSigned);

    static bool isKind(SymbolKind kind) { return kind == SymbolKind::PackedStructType; }
};

/// Represents an unpacked structure of members.
class UnpackedStructType : public Type, public Scope {
public:
    explicit UnpackedStructType(Compilation& compilation);

    static const Type& fromSyntax(const Scope& scope, const StructUnionTypeSyntax& syntax);

    ConstantValue getDefaultValueImpl() const;

    static bool isKind(SymbolKind kind) { return kind == SymbolKind::UnpackedStructType; }
};

/// Represents a packed union of members.
class PackedUnionType : public IntegralType, public Scope {
public:
    PackedUnionType(Compilation& compilation, bitwidth_t bitWidth, bool isSigned, bool isFourState);

    static const Type& fromSyntax(Compilation& compilation, const StructUnionTypeSyntax& syntax,
                                  LookupLocation location, const Scope& scope, bool forceSigned);

    static bool isKind(SymbolKind kind) { return kind == SymbolKind::PackedUnionType; }
};

/// Represents an unpacked union of members.
class UnpackedUnionType : public Type, public Scope {
public:
    explicit UnpackedUnionType(Compilation& compilation);

    static const Type& fromSyntax(const Scope& scope, const StructUnionTypeSyntax& syntax);

    ConstantValue getDefaultValueImpl() const;

    static bool isKind(SymbolKind kind) { return kind == SymbolKind::UnpackedUnionType; }
};

/// Represents the Void (or lack of a) type. This can be used as the return type of functions
/// and as the type of members in tagged unions.
class VoidType : public Type {
public:
    VoidType() : Type(SymbolKind::VoidType, "void", SourceLocation()) {}

    ConstantValue getDefaultValueImpl() const { return nullptr; }

    static bool isKind(SymbolKind kind) { return kind == SymbolKind::VoidType; }
};

/// Represents the Null type. This can be used as a literal for setting class handles and
/// chandles to null (or the default value).
class NullType : public Type {
public:
    NullType() : Type(SymbolKind::NullType, "null", SourceLocation()) {}

    ConstantValue getDefaultValueImpl() const;

    static bool isKind(SymbolKind kind) { return kind == SymbolKind::NullType; }
};

/// Represents storage for pointers passed using the DPI (a "C" compatible handle).
class CHandleType : public Type {
public:
    CHandleType() : Type(SymbolKind::CHandleType, "chandle", SourceLocation()) {}

    ConstantValue getDefaultValueImpl() const;

    static bool isKind(SymbolKind kind) { return kind == SymbolKind::CHandleType; }
};

/// Represents an ASCII string type.
class StringType : public Type {
public:
    StringType() : Type(SymbolKind::StringType, "string", SourceLocation()) {}

    ConstantValue getDefaultValueImpl() const;

    static bool isKind(SymbolKind kind) { return kind == SymbolKind::StringType; }
};

/// Represents a SystemVerilog event handle, which is used for synchronization between
/// asynchronous processes.
class EventType : public Type {
public:
    EventType() : Type(SymbolKind::EventType, "event", SourceLocation()) {}

    ConstantValue getDefaultValueImpl() const;

    static bool isKind(SymbolKind kind) { return kind == SymbolKind::EventType; }
};

struct ForwardInterfaceClassTypedefDeclarationSyntax;
struct ForwardTypedefDeclarationSyntax;

/// A forward declaration of a user-defined type name. A given type name can have
/// an arbitrary number of forward declarations in the same scope, so each symbol
/// forms a linked list, headed by the actual type definition.
class ForwardingTypedefSymbol : public Symbol {
public:
#define CATEGORY(x) x(None) x(Enum) x(Struct) x(Union) x(Class) x(InterfaceClass)
    ENUM_MEMBER(Category, CATEGORY);
#undef CATEGORY

    Category category;

    ForwardingTypedefSymbol(string_view name, SourceLocation loc, Category category) :
        Symbol(SymbolKind::ForwardingTypedef, name, loc), category(category) {}

    static const ForwardingTypedefSymbol& fromSyntax(const Scope& scope,
                                                     const ForwardTypedefDeclarationSyntax& syntax);

    static const ForwardingTypedefSymbol& fromSyntax(
        const Scope& scope, const ForwardInterfaceClassTypedefDeclarationSyntax& syntax);

    void addForwardDecl(const ForwardingTypedefSymbol& decl) const;
    const ForwardingTypedefSymbol* getNextForwardDecl() const { return next; }

    void serializeTo(ASTSerializer& serializer) const;

    static bool isKind(SymbolKind kind) { return kind == SymbolKind::ForwardingTypedef; }

private:
    mutable const ForwardingTypedefSymbol* next = nullptr;
};

struct TypedefDeclarationSyntax;

/// Represents a type alias, which is introduced via a typedef or type parameter.
class TypeAliasType : public Type {
public:
    DeclaredType targetType;

    TypeAliasType(string_view name, SourceLocation loc) :
        Type(SymbolKind::TypeAlias, name, loc), targetType(*this) {
        canonical = nullptr;
    }

    static const TypeAliasType& fromSyntax(const Scope& scope,
                                           const TypedefDeclarationSyntax& syntax);

    void addForwardDecl(const ForwardingTypedefSymbol& decl) const;
    const ForwardingTypedefSymbol* getFirstForwardDecl() const { return firstForward; }

    /// Checks all forward declarations for validity when considering the target type
    /// of this alias. Any inconsistencies will issue diagnostics.
    void checkForwardDecls() const;

    ConstantValue getDefaultValueImpl() const;

    void serializeTo(ASTSerializer& serializer) const;

    static bool isKind(SymbolKind kind) { return kind == SymbolKind::TypeAlias; }

private:
    friend class TypeParameterSymbol;

    mutable const ForwardingTypedefSymbol* firstForward = nullptr;
};

/// An empty type symbol that indicates an error occurred while trying to
/// resolve the type of some expression or declaration.
class ErrorType : public Type {
public:
    ErrorType() : Type(SymbolKind::ErrorType, "", SourceLocation()) {}

    ConstantValue getDefaultValueImpl() const { return nullptr; }

    static bool isKind(SymbolKind kind) { return kind == SymbolKind::ErrorType; }

    static const ErrorType Instance;
};

struct NetTypeDeclarationSyntax;

/// Base class for all net types in SystemVerilog.
///
/// There is a parallel type system for nets that exists independently from the data type
/// system. Most nets will be one of the built in types, but user defined net types can
/// exist too.
///
class NetType : public Symbol {
public:
    enum NetKind {
        Unknown,
        Wire,
        WAnd,
        WOr,
        Tri,
        TriAnd,
        TriOr,
        Tri0,
        Tri1,
        TriReg,
        Supply0,
        Supply1,
        UWire,
        UserDefined
    } netKind;

    NetType(NetKind netKind, string_view name, const Type& dataType);
    NetType(string_view name, SourceLocation location);

    /// If this net type is an alias, gets the target of the alias. Otherwise returns nullptr.
    const NetType* getAliasTarget() const;

    /// Gets the canonical net type for this net type, which involves unwrapping any aliases.
    const NetType& getCanonical() const;

    /// Gets the data type for nets of this particular net type.
    const Type& getDataType() const;

    /// Gets the custom resolution function for this net type, if it has one.
    const SubroutineSymbol* getResolutionFunction() const;

    bool isError() const { return netKind == Unknown; }
    bool isBuiltIn() const { return netKind != UserDefined; }

    void serializeTo(ASTSerializer& serializer) const;

    static NetType& fromSyntax(const Scope& scope, const NetTypeDeclarationSyntax& syntax);

    static bool isKind(SymbolKind kind) { return kind == SymbolKind::NetType; }

private:
    friend class Symbol;

    void resolve() const;

    mutable DeclaredType declaredType;

    mutable const NetType* alias = nullptr;
    mutable const SubroutineSymbol* resolver = nullptr;
    mutable bool isResolved = false;
};

} // namespace slang
