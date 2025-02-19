/*
    This file is part of solidity.

    solidity is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    solidity is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with solidity.  If not, see <http://www.gnu.org/licenses/>.
*/
/**
 * @author Christian <c@ethdev.com>
 * @date 2014
 * Solidity abstract syntax tree.
 */

#include <libsolidity/ast/AST.h>
#include <libsolidity/ast/ASTVisitor.h>
#include <libsolidity/ast/AST_accept.h>
#include <libsolidity/codegen/CompilerContext.h>

#include <libdevcore/Keccak256.h>

#include <boost/algorithm/string.hpp>

#include <algorithm>
#include <functional>

using namespace std;
using namespace dev;
using namespace dev::solidity;

class IDDispenser
{
public:
	static size_t next() { return ++instance(); }
	static void reset() { instance() = 0; }
private:
	static size_t& instance()
	{
		static IDDispenser dispenser;
		return dispenser.id;
	}
	size_t id = 0;
};

ASTNode::ASTNode(SourceLocation const& _location):
	m_id(IDDispenser::next()),
	m_location(_location)
{
}

ASTNode::~ASTNode()
{
	delete m_annotation;
}

void ASTNode::resetID()
{
	IDDispenser::reset();
}

ASTAnnotation& ASTNode::annotation() const
{
	if (!m_annotation)
		m_annotation = new ASTAnnotation();
	return *m_annotation;
}

SourceUnitAnnotation& SourceUnit::annotation() const
{
	if (!m_annotation)
		m_annotation = new SourceUnitAnnotation();
	return dynamic_cast<SourceUnitAnnotation&>(*m_annotation);
}

set<SourceUnit const*> SourceUnit::referencedSourceUnits(bool _recurse, set<SourceUnit const*> _skipList) const
{
	set<SourceUnit const*> sourceUnits;
	for (ImportDirective const* importDirective: filteredNodes<ImportDirective>(nodes()))
	{
		auto const& sourceUnit = importDirective->annotation().sourceUnit;
		if (!_skipList.count(sourceUnit))
		{
			_skipList.insert(sourceUnit);
			sourceUnits.insert(sourceUnit);
			if (_recurse)
				sourceUnits += sourceUnit->referencedSourceUnits(true, _skipList);
		}
	}
	return sourceUnits;
}

ImportAnnotation& ImportDirective::annotation() const
{
	if (!m_annotation)
		m_annotation = new ImportAnnotation();
	return dynamic_cast<ImportAnnotation&>(*m_annotation);
}

TypePointer ImportDirective::type() const
{
	solAssert(!!annotation().sourceUnit, "");
	return make_shared<ModuleType>(*annotation().sourceUnit);
}

map<FixedHash<4>, FunctionTypePointer> ContractDefinition::interfaceFunctions() const
{
	auto exportedFunctionList = interfaceFunctionList();

	map<FixedHash<4>, FunctionTypePointer> exportedFunctions;
	for (auto const& it: exportedFunctionList)
		exportedFunctions.insert(it);

	solAssert(
		exportedFunctionList.size() == exportedFunctions.size(),
		"Hash collision at Function Definition Hash calculation"
	);

	return exportedFunctions;
}

FunctionDefinition const* ContractDefinition::constructor() const
{
	for (FunctionDefinition const* f: definedFunctions())
		if (f->isConstructor())
			return f;
	return nullptr;
}

bool ContractDefinition::constructorIsPublic() const
{
	FunctionDefinition const* f = constructor();
	return !f || f->isPublic();
}

FunctionDefinition const* ContractDefinition::fallbackFunction() const
{
	for (ContractDefinition const* contract: annotation().linearizedBaseContracts)
		for (FunctionDefinition const* f: contract->definedFunctions())
			if (f->isFallback())
				return f;
	return nullptr;
}

vector<EventDefinition const*> const& ContractDefinition::interfaceEvents() const
{
	if (!m_interfaceEvents)
	{
		set<string> eventsSeen;
		m_interfaceEvents.reset(new vector<EventDefinition const*>());
		for (ContractDefinition const* contract: annotation().linearizedBaseContracts)
			for (EventDefinition const* e: contract->events())
			{
				/// NOTE: this requires the "internal" version of an Event,
				///       though here internal strictly refers to visibility,
				///       and not to function encoding (jump vs. call)
				auto const& function = e->functionType(true);
				solAssert(function, "");
				string eventSignature = function->externalSignature();
				if (eventsSeen.count(eventSignature) == 0)
				{
					eventsSeen.insert(eventSignature);
					m_interfaceEvents->push_back(e);
				}
			}
	}
	return *m_interfaceEvents;
}

vector<pair<FixedHash<4>, FunctionTypePointer>> const& ContractDefinition::interfaceFunctionList() const
{
	if (!m_interfaceFunctionList)
	{
		set<string> signaturesSeen;
		m_interfaceFunctionList.reset(new vector<pair<FixedHash<4>, FunctionTypePointer>>());
		for (ContractDefinition const* contract: annotation().linearizedBaseContracts)
		{
			vector<FunctionTypePointer> functions;
			for (FunctionDefinition const* f: contract->definedFunctions())
				if (f->isPartOfExternalInterface())
					functions.push_back(make_shared<FunctionType>(*f, false, false));
			for (VariableDeclaration const* v: contract->stateVariables())
				if (v->isPartOfExternalInterface())
					functions.push_back(make_shared<FunctionType>(*v));
			for (FunctionTypePointer const& fun: functions)
			{
				if (!fun->interfaceFunctionType())
					// Fails hopefully because we already registered the error
					continue;
				string functionSignature = fun->externalSignature();
				if (signaturesSeen.count(functionSignature) == 0)
				{
					signaturesSeen.insert(functionSignature);
					FixedHash<4> hash(dev::keccak256(functionSignature));
					m_interfaceFunctionList->push_back(make_pair(hash, fun));
				}
			}
		}
	}
	return *m_interfaceFunctionList;
}

vector<Declaration const*> const& ContractDefinition::inheritableMembers() const
{
	if (!m_inheritableMembers)
	{
		set<string> memberSeen;
		m_inheritableMembers.reset(new vector<Declaration const*>());
		auto addInheritableMember = [&](Declaration const* _decl)
		{
			solAssert(_decl, "addInheritableMember got a nullpointer.");
			if (memberSeen.count(_decl->name()) == 0 && _decl->isVisibleInDerivedContracts())
			{
				memberSeen.insert(_decl->name());
				m_inheritableMembers->push_back(_decl);
			}
		};

		for (FunctionDefinition const* f: definedFunctions())
			addInheritableMember(f);

		for (VariableDeclaration const* v: stateVariables())
			addInheritableMember(v);

		for (StructDefinition const* s: definedStructs())
			addInheritableMember(s);

		for (EnumDefinition const* e: definedEnums())
			addInheritableMember(e);

		for (EventDefinition const* e: events())
			addInheritableMember(e);
	}
	return *m_inheritableMembers;
}

TypePointer ContractDefinition::type() const
{
	return make_shared<TypeType>(make_shared<ContractType>(*this));
}

ContractDefinitionAnnotation& ContractDefinition::annotation() const
{
	if (!m_annotation)
		m_annotation = new ContractDefinitionAnnotation();
	return dynamic_cast<ContractDefinitionAnnotation&>(*m_annotation);
}

std::vector<Rule const*> ContractDefinition::rules() const
{
	std::vector<Rule const*> res = filteredNodes<Rule>(m_subNodes);
	std::sort(res.begin(), res.end(), [](Rule const* a, Rule const* b){return a->salience()>b->salience();});
	return res;
}

TypeNameAnnotation& TypeName::annotation() const
{
	if (!m_annotation)
		m_annotation = new TypeNameAnnotation();
	return dynamic_cast<TypeNameAnnotation&>(*m_annotation);
}

TypePointer StructDefinition::type() const
{
	return make_shared<TypeType>(make_shared<StructType>(*this));
}

TypeDeclarationAnnotation& StructDefinition::annotation() const
{
	if (!m_annotation)
		m_annotation = new TypeDeclarationAnnotation();
	return dynamic_cast<TypeDeclarationAnnotation&>(*m_annotation);
}

TypePointer EnumValue::type() const
{
	auto parentDef = dynamic_cast<EnumDefinition const*>(scope());
	solAssert(parentDef, "Enclosing Scope of EnumValue was not set");
	return make_shared<EnumType>(*parentDef);
}

TypePointer EnumDefinition::type() const
{
	return make_shared<TypeType>(make_shared<EnumType>(*this));
}

TypeDeclarationAnnotation& EnumDefinition::annotation() const
{
	if (!m_annotation)
		m_annotation = new TypeDeclarationAnnotation();
	return dynamic_cast<TypeDeclarationAnnotation&>(*m_annotation);
}

ContractDefinition::ContractKind FunctionDefinition::inContractKind() const
{
	auto contractDef = dynamic_cast<ContractDefinition const*>(scope());
	solAssert(contractDef, "Enclosing Scope of FunctionDefinition was not set.");
	return contractDef->contractKind();
}

FunctionTypePointer FunctionDefinition::functionType(bool _internal) const
{
	if (_internal)
	{
		switch (visibility())
		{
		case Declaration::Visibility::Default:
			solAssert(false, "visibility() should not return Default");
		case Declaration::Visibility::Private:
		case Declaration::Visibility::Internal:
		case Declaration::Visibility::Public:
			return make_shared<FunctionType>(*this, _internal);
		case Declaration::Visibility::External:
			return {};
		}
	}
	else
	{
		switch (visibility())
		{
		case Declaration::Visibility::Default:
			solAssert(false, "visibility() should not return Default");
		case Declaration::Visibility::Private:
		case Declaration::Visibility::Internal:
			return {};
		case Declaration::Visibility::Public:
		case Declaration::Visibility::External:
			return make_shared<FunctionType>(*this, _internal);
		}
	}

	// To make the compiler happy
	return {};
}

TypePointer FunctionDefinition::type() const
{
	solAssert(visibility() != Declaration::Visibility::External, "");
	return make_shared<FunctionType>(*this);
}

string FunctionDefinition::externalSignature() const
{
	return FunctionType(*this).externalSignature();
}

FunctionDefinitionAnnotation& FunctionDefinition::annotation() const
{
	if (!m_annotation)
		m_annotation = new FunctionDefinitionAnnotation();
	return dynamic_cast<FunctionDefinitionAnnotation&>(*m_annotation);
}

TypePointer ModifierDefinition::type() const
{
	return make_shared<ModifierType>(*this);
}

ModifierDefinitionAnnotation& ModifierDefinition::annotation() const
{
	if (!m_annotation)
		m_annotation = new ModifierDefinitionAnnotation();
	return dynamic_cast<ModifierDefinitionAnnotation&>(*m_annotation);
}

TypePointer EventDefinition::type() const
{
	return make_shared<FunctionType>(*this);
}

FunctionTypePointer EventDefinition::functionType(bool _internal) const
{
	if (_internal)
		return make_shared<FunctionType>(*this);
	else
		return {};
}

EventDefinitionAnnotation& EventDefinition::annotation() const
{
	if (!m_annotation)
		m_annotation = new EventDefinitionAnnotation();
	return dynamic_cast<EventDefinitionAnnotation&>(*m_annotation);
}

UserDefinedTypeNameAnnotation& UserDefinedTypeName::annotation() const
{
	if (!m_annotation)
		m_annotation = new UserDefinedTypeNameAnnotation();
	return dynamic_cast<UserDefinedTypeNameAnnotation&>(*m_annotation);
}

SourceUnit const& Scopable::sourceUnit() const
{
	ASTNode const* s = scope();
	solAssert(s, "");
	// will not always be a declaration
	while (dynamic_cast<Scopable const*>(s) && dynamic_cast<Scopable const*>(s)->scope())
		s = dynamic_cast<Scopable const*>(s)->scope();
	return dynamic_cast<SourceUnit const&>(*s);
}

string Scopable::sourceUnitName() const
{
	return sourceUnit().annotation().path;
}

bool VariableDeclaration::isLValue() const
{
	// External function parameters and constant declared variables are Read-Only
	return !isExternalCallableParameter() && !m_isConstant;
}

bool VariableDeclaration::isLocalVariable() const
{
	auto s = scope();
	return
		dynamic_cast<FunctionTypeName const*>(s) ||
		dynamic_cast<CallableDeclaration const*>(s) ||
		dynamic_cast<Block const*>(s) ||
		dynamic_cast<ForStatement const*>(s);
}

bool VariableDeclaration::isCallableParameter() const
{
	if (isReturnParameter())
		return true;

	vector<ASTPointer<VariableDeclaration>> const* parameters = nullptr;

	if (auto const* funTypeName = dynamic_cast<FunctionTypeName const*>(scope()))
		parameters = &funTypeName->parameterTypes();
	else if (auto const* callable = dynamic_cast<CallableDeclaration const*>(scope()))
		parameters = &callable->parameters();

	if (parameters)
		for (auto const& variable: *parameters)
			if (variable.get() == this)
				return true;
	return false;
}

bool VariableDeclaration::isLocalOrReturn() const
{
	return isReturnParameter() || (isLocalVariable() && !isCallableParameter());
}

bool VariableDeclaration::isReturnParameter() const
{
	vector<ASTPointer<VariableDeclaration>> const* returnParameters = nullptr;

	if (auto const* funTypeName = dynamic_cast<FunctionTypeName const*>(scope()))
		returnParameters = &funTypeName->returnParameterTypes();
	else if (auto const* callable = dynamic_cast<CallableDeclaration const*>(scope()))
		if (callable->returnParameterList())
			returnParameters = &callable->returnParameterList()->parameters();

	if (returnParameters)
		for (auto const& variable: *returnParameters)
			if (variable.get() == this)
				return true;
	return false;
}

bool VariableDeclaration::isExternalCallableParameter() const
{
	if (!isCallableParameter())
		return false;

	if (auto const* callable = dynamic_cast<CallableDeclaration const*>(scope()))
		if (callable->visibility() == Declaration::Visibility::External)
			return !isReturnParameter();

	return false;
}

bool VariableDeclaration::isInternalCallableParameter() const
{
	if (!isCallableParameter())
		return false;

	if (auto const* funTypeName = dynamic_cast<FunctionTypeName const*>(scope()))
		return funTypeName->visibility() == Declaration::Visibility::Internal;
	else if (auto const* callable = dynamic_cast<CallableDeclaration const*>(scope()))
		return callable->visibility() <= Declaration::Visibility::Internal;
	return false;
}

bool VariableDeclaration::isLibraryFunctionParameter() const
{
	if (!isCallableParameter())
		return false;
	if (auto const* funDef = dynamic_cast<FunctionDefinition const*>(scope()))
		return dynamic_cast<ContractDefinition const&>(*funDef->scope()).isLibrary();
	else
		return false;
}

bool VariableDeclaration::isEventParameter() const
{
	return dynamic_cast<EventDefinition const*>(scope()) != nullptr;
}

bool VariableDeclaration::hasReferenceOrMappingType() const
{
	solAssert(typeName(), "");
	solAssert(typeName()->annotation().type, "Can only be called after reference resolution");
	TypePointer const& type = typeName()->annotation().type;
	return type->category() == Type::Category::Mapping || dynamic_cast<ReferenceType const*>(type.get());
}

set<VariableDeclaration::Location> VariableDeclaration::allowedDataLocations() const
{
	using Location = VariableDeclaration::Location;

	if (!hasReferenceOrMappingType() || isStateVariable() || isEventParameter())
		return set<Location>{ Location::Unspecified };
	else if (isStateVariable() && isConstant())
		return set<Location>{ Location::Memory };
	else if (isExternalCallableParameter())
	{
		set<Location> locations{ Location::CallData };
		if (isLibraryFunctionParameter())
			locations.insert(Location::Storage);
		return locations;
	}
	else if (isCallableParameter())
	{
		set<Location> locations{ Location::Memory };
		if (isInternalCallableParameter() || isLibraryFunctionParameter())
			locations.insert(Location::Storage);
		return locations;
	}
	else if (isLocalVariable())
	{
		solAssert(typeName(), "");
		solAssert(typeName()->annotation().type, "Can only be called after reference resolution");
		if (typeName()->annotation().type->category() == Type::Category::Mapping)
			return set<Location>{ Location::Storage };
		else
			//  TODO: add Location::Calldata once implemented for local variables.
			return set<Location>{ Location::Memory, Location::Storage };
	}
	else
		// Struct members etc.
		return set<Location>{ Location::Unspecified };
}

TypePointer VariableDeclaration::type() const
{
	return annotation().type;
}

FunctionTypePointer VariableDeclaration::functionType(bool _internal) const
{
	if (_internal)
		return {};
	switch (visibility())
	{
	case Declaration::Visibility::Default:
		solAssert(false, "visibility() should not return Default");
	case Declaration::Visibility::Private:
	case Declaration::Visibility::Internal:
		return {};
	case Declaration::Visibility::Public:
	case Declaration::Visibility::External:
		return make_shared<FunctionType>(*this);
	}

	// To make the compiler happy
	return {};
}

VariableDeclarationAnnotation& VariableDeclaration::annotation() const
{
	if (!m_annotation)
		m_annotation = new VariableDeclarationAnnotation();
	return dynamic_cast<VariableDeclarationAnnotation&>(*m_annotation);
}

StatementAnnotation& Statement::annotation() const
{
	if (!m_annotation)
		m_annotation = new StatementAnnotation();
	return dynamic_cast<StatementAnnotation&>(*m_annotation);
}

InlineAssemblyAnnotation& InlineAssembly::annotation() const
{
	if (!m_annotation)
		m_annotation = new InlineAssemblyAnnotation();
	return dynamic_cast<InlineAssemblyAnnotation&>(*m_annotation);
}

ReturnAnnotation& Return::annotation() const
{
	if (!m_annotation)
		m_annotation = new ReturnAnnotation();
	return dynamic_cast<ReturnAnnotation&>(*m_annotation);
}

ExpressionAnnotation& Expression::annotation() const
{
	if (!m_annotation)
		m_annotation = new ExpressionAnnotation();
	return dynamic_cast<ExpressionAnnotation&>(*m_annotation);
}

MemberAccessAnnotation& MemberAccess::annotation() const
{
	if (!m_annotation)
		m_annotation = new MemberAccessAnnotation();
	return dynamic_cast<MemberAccessAnnotation&>(*m_annotation);
}

BinaryOperationAnnotation& BinaryOperation::annotation() const
{
	if (!m_annotation)
		m_annotation = new BinaryOperationAnnotation();
	return dynamic_cast<BinaryOperationAnnotation&>(*m_annotation);
}

FunctionCallAnnotation& FunctionCall::annotation() const
{
	if (!m_annotation)
		m_annotation = new FunctionCallAnnotation();
	return dynamic_cast<FunctionCallAnnotation&>(*m_annotation);
}

FireAllRulesAnnotation& FireAllRulesStatement::annotation() const
{
	if (!m_annotation)
		m_annotation = new FireAllRulesAnnotation();
	return dynamic_cast<FireAllRulesAnnotation&>(*m_annotation);
}

bool Identifier::saveToENISection(ENIHandler& _handler, CompilerContext& _context) const
{
	CompilerContext::LocationSetter locationSetter(_context, *this);
	Declaration const* declaration = this->annotation().referencedDeclaration;
	auto variable = dynamic_cast<VariableDeclaration const*>(declaration);
	_handler.appendIdentifier(variable->annotation().type, *variable, static_cast<Expression const&>(*this));

	if (!variable->isConstant()) {
		/// variable is not a constant.
		if (_context.isLocalVariable(declaration)) {
			/// local variable
		} else if (_context.isStateVariable(declaration)) {
			/// state variable
		} else {
			solUnimplementedAssert(false, "Unsupported identifier type\n");
		}
	} else {
		solUnimplementedAssert(false, "Should handler constant identifier\n");
	}
	_handler.setContext(&_context);
	return true;
}

IdentifierAnnotation& Identifier::annotation() const
{
	if (!m_annotation)
		m_annotation = new IdentifierAnnotation();
	return dynamic_cast<IdentifierAnnotation&>(*m_annotation);
}

ASTString Literal::valueWithoutUnderscores() const
{
	return boost::erase_all_copy(value(), "_");
}

bool Literal::isHexNumber() const
{
	if (token() != Token::Number)
		return false;
	return boost::starts_with(value(), "0x");
}

bool Literal::looksLikeAddress() const
{
	if (subDenomination() != SubDenomination::None)
		return false;

	if (!isHexNumber())
		return false;

	return abs(int(valueWithoutUnderscores().length()) - 42) <= 1;
}

bool Literal::passesAddressChecksum() const
{
	solAssert(isHexNumber(), "Expected hex number");
	return dev::passesAddressChecksum(valueWithoutUnderscores(), true);
}

string Literal::getChecksummedAddress() const
{
	solAssert(isHexNumber(), "Expected hex number");
	/// Pad literal to be a proper hex address.
	string address = valueWithoutUnderscores().substr(2);
	if (address.length() > 40)
		return string();
	address.insert(address.begin(), 40 - address.size(), '0');
	return dev::getChecksummedAddress(address);
}

bool Literal::saveToENISection(ENIHandler& _handler, CompilerContext& _context) const
{
	_handler.appendLiteral(m_token, *m_value);
	_handler.setContext(&_context);
	return true;
}

TypePointer Rule::type() const
{
	return make_shared<TypeType>(make_shared<RuleType>(*this));
}

TypePointer FactDeclaration::type() const
{
	return m_typeName->annotation().type;
}

void FieldExpression::replaceChild(Expression *oldExp, ASTPointer<Expression> newExp)
{
	if(m_expression.get()==oldExp)
		m_expression = newExp;
}

void Conditional::replaceChild(Expression *oldExp, ASTPointer<Expression> newExp)
{
	if(m_condition.get()==oldExp) m_condition = newExp;
	if(m_trueExpression.get()==oldExp) m_trueExpression = newExp;
	if(m_falseExpression.get()==oldExp) m_falseExpression = newExp;
}

void Assignment::replaceChild(Expression *oldExp, ASTPointer<Expression> newExp)
{
	if(m_leftHandSide.get()==oldExp) m_leftHandSide = newExp;
	if(m_rightHandSide.get()==oldExp) m_rightHandSide = newExp;
}

void TupleExpression::replaceChild(Expression *oldExp, ASTPointer<Expression> newExp)
{
	for(auto& exp: m_components){
		if(exp.get()==oldExp) exp = newExp;
	}
}

void UnaryOperation::replaceChild(Expression *oldExp, ASTPointer<Expression> newExp)
{
	if(m_subExpression.get()==oldExp) m_subExpression = newExp;
}

void BinaryOperation::replaceChild(Expression *oldExp, ASTPointer<Expression> newExp)
{
	if(m_left.get()==oldExp) m_left = newExp;
	if(m_right.get()==oldExp) m_right = newExp;
}

void FunctionCall::replaceChild(Expression *oldExp, ASTPointer<Expression> newExp)
{
	if(m_expression.get()==oldExp) m_expression = newExp;
}

void NewExpression::replaceChild(Expression*, ASTPointer<Expression>)
{

}

void MemberAccess::replaceChild(Expression *oldExp, ASTPointer<Expression> newExp)
{
	if(m_expression.get()==oldExp) m_expression = newExp;
}

void IndexAccess::replaceChild(Expression *oldExp, ASTPointer<Expression> newExp)
{
	if(m_base.get()==oldExp) m_base = newExp;
	if(m_index.get()==oldExp) m_index = newExp;
}
