//------------------------------------------------------------------------------
// DefinitionSymbols.cpp
// Contains definition-related symbol definitions
//
// File is under the MIT license; see LICENSE for details
//------------------------------------------------------------------------------
#include "slang/symbols/DefinitionSymbols.h"

#include "slang/binding/Expression.h"
#include "slang/compilation/Compilation.h"
#include "slang/diagnostics/DeclarationsDiags.h"
#include "slang/diagnostics/LookupDiags.h"
#include "slang/symbols/ASTSerializer.h"
#include "slang/symbols/AllTypes.h"
#include "slang/symbols/MemberSymbols.h"
#include "slang/symbols/ParameterSymbols.h"
#include "slang/symbols/Type.h"
#include "slang/symbols/VariableSymbols.h"
#include "slang/syntax/AllSyntax.h"
#include "slang/util/StackContainer.h"

namespace slang {

DefinitionSymbol::DefinitionSymbol(Compilation& compilation, string_view name, SourceLocation loc,
                                   DefinitionKind definitionKind, const NetType& defaultNetType) :
    Symbol(SymbolKind::Definition, name, loc),
    Scope(compilation, this), definitionKind(definitionKind), defaultNetType(defaultNetType),
    portMap(compilation.allocSymbolMap()) {
}

const ModportSymbol* DefinitionSymbol::getModportOrError(string_view modport, const Scope& scope,
                                                         SourceRange range) const {
    if (modport.empty())
        return nullptr;

    auto symbol = find(modport);
    if (!symbol) {
        auto& diag = scope.addDiag(diag::UnknownMember, range);
        diag << modport;
        diag << this->name;
        return nullptr;
    }

    if (symbol->kind != SymbolKind::Modport) {
        auto& diag = scope.addDiag(diag::NotAModport, range);
        diag << modport;
        diag.addNote(diag::NoteDeclarationHere, symbol->location);
        return nullptr;
    }

    return &symbol->as<ModportSymbol>();
}

DefinitionSymbol& DefinitionSymbol::fromSyntax(Compilation& compilation,
                                               const ModuleDeclarationSyntax& syntax,
                                               const Scope& scope) {
    auto nameToken = syntax.header->name;
    auto result = compilation.emplace<DefinitionSymbol>(
        compilation, nameToken.valueText(), nameToken.location(),
        SemanticFacts::getDefinitionKind(syntax.kind), compilation.getDefaultNetType(syntax));

    result->setSyntax(syntax);
    result->setAttributes(scope, syntax.attributes);

    for (auto import : syntax.header->imports)
        result->addMembers(*import);

    SmallVectorSized<const ParameterSymbolBase*, 8> parameters;
    bool hasPortParams = syntax.header->parameters;
    if (hasPortParams) {
        bool lastLocal = false;
        for (auto declaration : syntax.header->parameters->declarations) {
            // It's legal to leave off the parameter keyword in the parameter port list.
            // If you do so, we "inherit" the parameter or localparam keyword from the previous
            // entry. This isn't allowed in a module body, but the parser will take care of the
            // error for us.
            if (declaration->keyword)
                lastLocal = declaration->keyword.kind == TokenKind::LocalParamKeyword;

            if (declaration->kind == SyntaxKind::ParameterDeclaration) {
                SmallVectorSized<ParameterSymbol*, 8> params;
                ParameterSymbol::fromSyntax(*result, declaration->as<ParameterDeclarationSyntax>(),
                                            lastLocal, /* isPort */ true, params);

                for (auto param : params) {
                    parameters.append(param);
                    result->addMember(*param);
                }
            }
            else {
                SmallVectorSized<TypeParameterSymbol*, 8> params;
                TypeParameterSymbol::fromSyntax(*result,
                                                declaration->as<TypeParameterDeclarationSyntax>(),
                                                lastLocal, /* isPort */ true, params);

                for (auto param : params) {
                    parameters.append(param);
                    result->addMember(*param);
                }
            }
        }
    }

    if (syntax.header->ports)
        result->addMembers(*syntax.header->ports);

    bool first = true;
    for (auto member : syntax.members) {
        if (member->kind == SyntaxKind::TimeUnitsDeclaration)
            result->setTimeScale(*result, member->as<TimeUnitsDeclarationSyntax>(), first);
        else if (member->kind != SyntaxKind::ParameterDeclarationStatement) {
            result->addMembers(*member);
            first = false;
        }
        else {
            first = false;

            auto declaration = member->as<ParameterDeclarationStatementSyntax>().parameter;
            bool isLocal =
                hasPortParams || declaration->keyword.kind == TokenKind::LocalParamKeyword;

            if (declaration->kind == SyntaxKind::ParameterDeclaration) {
                SmallVectorSized<ParameterSymbol*, 8> params;
                ParameterSymbol::fromSyntax(*result, declaration->as<ParameterDeclarationSyntax>(),
                                            isLocal, false, params);

                for (auto param : params) {
                    parameters.append(param);
                    result->addMember(*param);
                }
            }
            else {
                SmallVectorSized<TypeParameterSymbol*, 8> params;
                TypeParameterSymbol::fromSyntax(*result,
                                                declaration->as<TypeParameterDeclarationSyntax>(),
                                                isLocal, false, params);

                for (auto param : params) {
                    parameters.append(param);
                    result->addMember(*param);
                }
            }
        }
    }

    result->finalizeTimeScale(scope, syntax);
    result->parameters = parameters.copy(compilation);
    return *result;
}

void DefinitionSymbol::serializeTo(ASTSerializer& serializer) const {
    serializer.write("definitionKind", toString(definitionKind));
}

namespace {

Symbol* createInstance(Compilation& compilation, const Scope& scope,
                       const DefinitionSymbol& definition, const HierarchicalInstanceSyntax& syntax,
                       span<const ParameterSymbolBase* const> parameters,
                       SmallVector<int32_t>& path,
                       span<const AttributeInstanceSyntax* const> attributes,
                       uint32_t hierarchyDepth) {
    InstanceSymbol* inst;
    switch (definition.definitionKind) {
        case DefinitionKind::Module:
            inst = &ModuleInstanceSymbol::instantiate(compilation, syntax, definition, parameters,
                                                      hierarchyDepth);
            break;
        case DefinitionKind::Interface:
            inst = &InterfaceInstanceSymbol::instantiate(compilation, syntax, definition,
                                                         parameters, hierarchyDepth);
            break;
        case DefinitionKind::Program:
            inst = &ProgramInstanceSymbol::instantiate(compilation, syntax, definition, parameters,
                                                       hierarchyDepth);
            break;
        default:
            THROW_UNREACHABLE;
    }

    inst->arrayPath = path.copy(compilation);
    inst->setSyntax(syntax);
    inst->setAttributes(scope, attributes);
    return inst;
};

// TODO: clean this up

using DimIterator = span<VariableDimensionSyntax*>::iterator;

Symbol* recurseInstanceArray(Compilation& compilation, const DefinitionSymbol& definition,
                             const HierarchicalInstanceSyntax& instanceSyntax,
                             span<const ParameterSymbolBase* const> parameters,
                             const BindContext& context, DimIterator it, DimIterator end,
                             SmallVector<int32_t>& path,
                             span<const AttributeInstanceSyntax* const> attributes,
                             uint32_t hierarchyDepth) {
    if (it == end) {
        return createInstance(compilation, context.scope, definition, instanceSyntax, parameters,
                              path, attributes, hierarchyDepth);
    }

    // Evaluate the dimensions of the array. If this fails for some reason,
    // make up an empty array so that we don't get further errors when
    // things try to reference this symbol.
    auto nameToken = instanceSyntax.name;
    EvaluatedDimension dim = context.evalDimension(**it, true);
    if (!dim.isRange()) {
        return compilation.emplace<InstanceArraySymbol>(
            compilation, nameToken.valueText(), nameToken.location(), span<const Symbol* const>{},
            ConstantRange());
    }

    ++it;

    ConstantRange range = dim.range;
    SmallVectorSized<const Symbol*, 8> elements;
    for (int32_t i = range.lower(); i <= range.upper(); i++) {
        path.append(i);
        auto symbol = recurseInstanceArray(compilation, definition, instanceSyntax, parameters,
                                           context, it, end, path, attributes, hierarchyDepth);
        path.pop();

        symbol->name = "";
        elements.append(symbol);
    }

    auto result = compilation.emplace<InstanceArraySymbol>(compilation, nameToken.valueText(),
                                                           nameToken.location(),
                                                           elements.copy(compilation), range);
    for (auto element : elements)
        result->addMember(*element);

    return result;
}

Scope& createTempInstance(Compilation& compilation, const DefinitionSymbol& def) {
    // Construct a temporary scope that has the right parent to house instance parameters
    // as we're evaluating them. We hold on to the initializer expressions and give them
    // to the instances later when we create them.
    struct TempInstance : public ModuleInstanceSymbol {
        using ModuleInstanceSymbol::ModuleInstanceSymbol;
        void setParent(const Scope& scope) { ModuleInstanceSymbol::setParent(scope); }
    };

    auto& tempDef =
        *compilation.emplace<TempInstance>(compilation, def.name, def.location, def, 0u);
    tempDef.setParent(*def.getParentScope());

    // Need the imports here as well, since parameters may depend on them.
    for (auto import : def.getSyntax()->as<ModuleDeclarationSyntax>().header->imports)
        tempDef.addMembers(*import);

    return tempDef;
}

void createImplicitNets(const HierarchicalInstanceSyntax& instance, const BindContext& context,
                        const NetType& netType, SmallSet<string_view, 8>& implicitNetNames,
                        SmallVector<const Symbol*>& results) {
    // If no default nettype is set, we don't create implicit nets.
    if (netType.isError())
        return;

    for (auto conn : instance.connections) {
        const ExpressionSyntax* expr = nullptr;
        switch (conn->kind) {
            case SyntaxKind::OrderedPortConnection:
                expr = conn->as<OrderedPortConnectionSyntax>().expr;
                break;
            case SyntaxKind::NamedPortConnection:
                expr = conn->as<NamedPortConnectionSyntax>().expr;
                break;
            default:
                break;
        }

        if (!expr)
            continue;

        SmallVectorSized<Token, 8> implicitNets;
        Expression::findPotentiallyImplicitNets(*expr, context, implicitNets);

        for (Token t : implicitNets) {
            if (implicitNetNames.emplace(t.valueText()).second) {
                auto& comp = context.getCompilation();
                auto net = comp.emplace<NetSymbol>(t.valueText(), t.location(), netType);
                net->setType(comp.getLogicType());
                results.append(net);
            }
        }
    }
}

} // namespace

void InstanceSymbol::fromSyntax(Compilation& compilation,
                                const HierarchyInstantiationSyntax& syntax, LookupLocation location,
                                const Scope& scope, SmallVector<const Symbol*>& results) {

    auto definition = compilation.getDefinition(syntax.type.valueText(), scope);
    if (!definition) {
        scope.addDiag(diag::UnknownModule, syntax.type.range()) << syntax.type.valueText();
        return;
    }

    SmallMap<string_view, const ExpressionSyntax*, 8> paramOverrides;
    if (syntax.parameters) {
        // Build up data structures to easily index the parameter assignments. We need to handle
        // both ordered assignment as well as named assignment, though a specific instance can only
        // use one method or the other.
        bool hasParamAssignments = false;
        bool orderedAssignments = true;
        SmallVectorSized<const OrderedArgumentSyntax*, 8> orderedParams;
        SmallMap<string_view, std::pair<const NamedArgumentSyntax*, bool>, 8> namedParams;

        for (auto paramBase : syntax.parameters->assignments->parameters) {
            bool isOrdered = paramBase->kind == SyntaxKind::OrderedArgument;
            if (!hasParamAssignments) {
                hasParamAssignments = true;
                orderedAssignments = isOrdered;
            }
            else if (isOrdered != orderedAssignments) {
                scope.addDiag(diag::MixingOrderedAndNamedParams,
                              paramBase->getFirstToken().location());
                break;
            }

            if (isOrdered)
                orderedParams.append(&paramBase->as<OrderedArgumentSyntax>());
            else {
                const NamedArgumentSyntax& nas = paramBase->as<NamedArgumentSyntax>();
                auto name = nas.name.valueText();
                if (!name.empty()) {
                    auto pair = namedParams.emplace(name, std::make_pair(&nas, false));
                    if (!pair.second) {
                        auto& diag =
                            scope.addDiag(diag::DuplicateParamAssignment, nas.name.location());
                        diag << name;
                        diag.addNote(diag::NotePreviousUsage,
                                     pair.first->second.first->name.location());
                    }
                }
            }
        }

        // For each parameter assignment we have, match it up to a real parameter
        if (orderedAssignments) {
            uint32_t orderedIndex = 0;
            for (auto param : definition->parameters) {
                if (orderedIndex >= orderedParams.size())
                    break;

                if (param->isLocalParam())
                    continue;

                paramOverrides.emplace(param->symbol.name, orderedParams[orderedIndex++]->expr);
            }

            // Make sure there aren't extra param assignments for non-existent params.
            if (orderedIndex < orderedParams.size()) {
                auto loc = orderedParams[orderedIndex]->getFirstToken().location();
                auto& diag = scope.addDiag(diag::TooManyParamAssignments, loc);
                diag << definition->name;
                diag << orderedParams.size();
                diag << orderedIndex;
            }
        }
        else {
            // Otherwise handle named assignments.
            for (auto param : definition->parameters) {
                auto it = namedParams.find(param->symbol.name);
                if (it == namedParams.end())
                    continue;

                const NamedArgumentSyntax* arg = it->second.first;
                it->second.second = true;
                if (param->isLocalParam()) {
                    // Can't assign to localparams, so this is an error.
                    DiagCode code = param->isPortParam() ? diag::AssignedToLocalPortParam
                                                         : diag::AssignedToLocalBodyParam;

                    auto& diag = scope.addDiag(code, arg->name.location());
                    diag.addNote(diag::NoteDeclarationHere, param->symbol.location);
                    continue;
                }

                // It's allowed to have no initializer in the assignment; it means to just use the
                // default.
                if (!arg->expr)
                    continue;

                paramOverrides.emplace(param->symbol.name, arg->expr);
            }

            for (const auto& pair : namedParams) {
                // We marked all the args that we used, so anything left over is a param assignment
                // for a non-existent parameter.
                if (!pair.second.second) {
                    auto& diag = scope.addDiag(diag::ParameterDoesNotExist,
                                               pair.second.first->name.location());
                    diag << pair.second.first->name.valueText();
                    diag << definition->name;
                }
            }
        }
    }

    // As an optimization, determine values for all parameters now so that they can be
    // shared between instances. That way an instance array with hundreds of entries
    // doesn't recompute the same param values over and over again.
    Scope& tempDef = createTempInstance(compilation, *definition);

    BindContext context(scope, location, BindFlags::Constant);
    SmallVectorSized<const ParameterSymbolBase*, 8> parameters;

    for (auto param : definition->parameters) {
        if (param->symbol.kind == SymbolKind::Parameter) {
            // This is a value parameter.
            ParameterSymbol& newParam = param->symbol.as<ParameterSymbol>().clone(compilation);
            tempDef.addMember(newParam);
            parameters.append(&newParam);

            if (auto it = paramOverrides.find(newParam.name); it != paramOverrides.end()) {
                auto& expr = *it->second;
                newParam.setInitializerSyntax(expr, expr.getFirstToken().location());

                auto declared = newParam.getDeclaredType();
                declared->clearResolved();
                declared->resolveAt(context);
            }
            else if (!newParam.isLocalParam() && newParam.isPortParam() &&
                     !newParam.getInitializer()) {
                auto& diag =
                    scope.addDiag(diag::ParamHasNoValue, syntax.getFirstToken().location());
                diag << definition->name;
                diag << newParam.name;
            }
            else {
                newParam.getDeclaredType()->clearResolved();
            }
        }
        else {
            // Otherwise this is a type parameter.
            auto& newParam = param->symbol.as<TypeParameterSymbol>().clone(compilation);
            tempDef.addMember(newParam);
            parameters.append(&newParam);

            auto& declared = newParam.targetType;

            if (auto it = paramOverrides.find(newParam.name); it != paramOverrides.end()) {
                auto& expr = *it->second;

                // If this is a NameSyntax, the parser didn't know we were assigning to
                // a type parameter, so fix it up into a NamedTypeSyntax to get a type from it.
                if (NameSyntax::isKind(expr.kind)) {
                    // const_cast is ugly but safe here, we're only going to refer to it
                    // by const reference everywhere down.
                    auto& nameSyntax = const_cast<NameSyntax&>(expr.as<NameSyntax>());
                    auto namedType = compilation.emplace<NamedTypeSyntax>(nameSyntax);
                    declared.setType(compilation.getType(*namedType, location, scope));
                }
                else if (!DataTypeSyntax::isKind(expr.kind)) {
                    scope.addDiag(diag::BadTypeParamExpr, expr.getFirstToken().location())
                        << newParam.name;
                    declared.clearResolved();
                }
                else {
                    declared.setType(
                        compilation.getType(expr.as<DataTypeSyntax>(), location, scope));
                }
            }
            else if (!newParam.isLocalParam() && newParam.isPortParam() &&
                     !declared.getTypeSyntax()) {
                auto& diag =
                    scope.addDiag(diag::ParamHasNoValue, syntax.getFirstToken().location());
                diag << definition->name;
                diag << newParam.name;
            }
            else {
                declared.clearResolved();
            }
        }
    }

    // In order to avoid infinitely recursive instantiations, keep track of how deep we are
    // in the hierarchy tree. Each instance knows, so we only need to walk up as far as our
    // nearest parent in order to know our own depth here.
    uint32_t hierarchyDepth = 0;
    const Symbol* parent = &scope.asSymbol();
    while (true) {
        if (InstanceSymbol::isKind(parent->kind)) {
            hierarchyDepth = parent->as<InstanceSymbol>().hierarchyDepth + 1;
            if (hierarchyDepth > compilation.getOptions().maxInstanceDepth) {
                auto& diag = scope.addDiag(diag::MaxInstanceDepthExceeded, syntax.type.range());
                diag << compilation.getOptions().maxInstanceDepth;
                return;
            }
            break;
        }

        auto s = parent->getParentScope();
        if (!s)
            break;

        parent = &s->asSymbol();
    }

    // We have to check each port connection expression for any names that can't be resolved,
    // which represent implicit nets that need to be created now.
    SmallSet<string_view, 8> implicitNetNames;
    auto& netType = scope.getDefaultNetType();

    for (auto instanceSyntax : syntax.instances) {
        createImplicitNets(*instanceSyntax, context, netType, implicitNetNames, results);

        SmallVectorSized<int32_t, 4> path;
        auto dims = instanceSyntax->dimensions;
        auto symbol =
            recurseInstanceArray(compilation, *definition, *instanceSyntax, parameters, context,
                                 dims.begin(), dims.end(), path, syntax.attributes, hierarchyDepth);
        results.append(symbol);
    }
}

InstanceSymbol::InstanceSymbol(SymbolKind kind, Compilation& compilation, string_view name,
                               SourceLocation loc, const DefinitionSymbol& definition,
                               uint32_t hierarchyDepth) :
    Symbol(kind, name, loc),
    Scope(compilation, this), definition(definition), hierarchyDepth(hierarchyDepth),
    portMap(compilation.allocSymbolMap()) {
}

void InstanceSymbol::serializeTo(ASTSerializer& serializer) const {
    serializer.writeLink("definition", definition);
}

bool InstanceSymbol::isKind(SymbolKind kind) {
    switch (kind) {
        case SymbolKind::ModuleInstance:
        case SymbolKind::ProgramInstance:
        case SymbolKind::InterfaceInstance:
            return true;
        default:
            return false;
    }
}

void InstanceSymbol::populate(const HierarchicalInstanceSyntax* instanceSyntax,
                              span<const ParameterSymbolBase* const> parameters) {
    // TODO: getSyntax dependency
    auto& declSyntax = definition.getSyntax()->as<ModuleDeclarationSyntax>();
    Compilation& comp = getCompilation();

    // Package imports from the header always come first.
    for (auto import : declSyntax.header->imports)
        addMembers(*import);

    // Now add in all parameter ports.
    auto paramIt = parameters.begin();
    while (paramIt != parameters.end()) {
        auto original = *paramIt;
        if (!original->isPortParam())
            break;

        if (original->symbol.kind == SymbolKind::Parameter)
            addMember(original->symbol.as<ParameterSymbol>().clone(comp));
        else
            addMember(original->symbol.as<TypeParameterSymbol>().clone(comp));

        paramIt++;
    }

    // It's important that the port syntax is added before any body members, so that port
    // connections are elaborated before anything tries to depend on any interface port params.
    if (declSyntax.header->ports)
        addMembers(*declSyntax.header->ports);

    // Connect all ports to external sources.
    if (instanceSyntax)
        setPortConnections(instanceSyntax->connections);

    // Finally add members from the body.
    for (auto member : declSyntax.members) {
        // If this is a parameter declaration, we should already have metadata for it in our
        // parameters list. The list is given in declaration order, so we should be be able to move
        // through them incrementally.
        if (member->kind != SyntaxKind::ParameterDeclarationStatement)
            addMembers(*member);
        else {
            auto paramBase = member->as<ParameterDeclarationStatementSyntax>().parameter;
            if (paramBase->kind == SyntaxKind::ParameterDeclaration) {
                for (auto declarator : paramBase->as<ParameterDeclarationSyntax>().declarators) {
                    ASSERT(paramIt != parameters.end());

                    auto& symbol = (*paramIt)->symbol;
                    ASSERT(declarator->name.valueText() == symbol.name);

                    addMember(symbol.as<ParameterSymbol>().clone(comp));
                    paramIt++;
                }
            }
            else {
                for (auto declarator :
                     paramBase->as<TypeParameterDeclarationSyntax>().declarators) {
                    ASSERT(paramIt != parameters.end());

                    auto& symbol = (*paramIt)->symbol;
                    ASSERT(declarator->name.valueText() == symbol.name);

                    addMember(symbol.as<TypeParameterSymbol>().clone(comp));
                    paramIt++;
                }
            }
        }
    }
}

ModuleInstanceSymbol& ModuleInstanceSymbol::instantiate(Compilation& compilation, string_view name,
                                                        SourceLocation loc,
                                                        const DefinitionSymbol& definition) {
    auto instance =
        compilation.emplace<ModuleInstanceSymbol>(compilation, name, loc, definition, 0u);
    instance->populate(nullptr, definition.parameters);
    return *instance;
}

ModuleInstanceSymbol& ModuleInstanceSymbol::instantiate(
    Compilation& compilation, const HierarchicalInstanceSyntax& syntax,
    const DefinitionSymbol& definition, span<const ParameterSymbolBase* const> parameters,
    uint32_t hierarchyDepth) {

    auto instance = compilation.emplace<ModuleInstanceSymbol>(
        compilation, syntax.name.valueText(), syntax.name.location(), definition, hierarchyDepth);
    instance->populate(&syntax, parameters);
    return *instance;
}

ProgramInstanceSymbol& ProgramInstanceSymbol::instantiate(
    Compilation& compilation, const HierarchicalInstanceSyntax& syntax,
    const DefinitionSymbol& definition, span<const ParameterSymbolBase* const> parameters,
    uint32_t hierarchyDepth) {

    auto instance = compilation.emplace<ProgramInstanceSymbol>(
        compilation, syntax.name.valueText(), syntax.name.location(), definition, hierarchyDepth);

    instance->populate(&syntax, parameters);
    return *instance;
}

InterfaceInstanceSymbol& InterfaceInstanceSymbol::instantiate(
    Compilation& compilation, const HierarchicalInstanceSyntax& syntax,
    const DefinitionSymbol& definition, span<const ParameterSymbolBase* const> parameters,
    uint32_t hierarchyDepth) {

    auto instance = compilation.emplace<InterfaceInstanceSymbol>(
        compilation, syntax.name.valueText(), syntax.name.location(), definition, hierarchyDepth);

    instance->populate(&syntax, parameters);
    return *instance;
}

void InstanceArraySymbol::serializeTo(ASTSerializer& serializer) const {
    serializer.write("range", range.toString());
}

} // namespace slang
