//
//  Type.c
//  Emojicode
//
//  Created by Theo Weidmann on 04.03.15.
//  Copyright (c) 2015 Theo Weidmann. All rights reserved.
//

#include <cstring>
#include <vector>
#include "utf8.h"
#include "Class.hpp"
#include "Function.hpp"
#include "EmojicodeCompiler.hpp"
#include "Enum.hpp"
#include "Protocol.hpp"
#include "TypeContext.hpp"
#include "ValueType.hpp"

//MARK: Globals
/* Very important one time declarations */

Class *CL_STRING;
Class *CL_LIST;
Class *CL_ERROR;
Class *CL_DATA;
Class *CL_DICTIONARY;
Protocol *PR_ENUMERATEABLE;
Protocol *PR_ENUMERATOR;
Class *CL_RANGE;
ValueType *VT_BOOLEAN;
ValueType *VT_SYMBOL;
ValueType *VT_INTEGER;
ValueType *VT_DOUBLE;

Type::Type(Protocol *p, bool o) : typeDefinition_(p), type_(TT_PROTOCOL), optional_(o)  {
}

Type::Type(Enum *e, bool o) : typeDefinition_(e), type_(TT_ENUM), optional_(o) {
}

Type::Type(ValueType *v, bool o) : typeDefinition_(v), type_(TT_VALUE_TYPE), optional_(o) {
}

Type::Type(Class *c, bool o) : typeDefinition_(c), type_(TT_CLASS), optional_(o) {
    for (int i = 0; i < c->numberOfGenericArgumentsWithSuperArguments(); i++) {
        genericArguments.push_back(Type(TT_REFERENCE, false, i, c));
    }
}

Class* Type::eclass() const {
    return static_cast<Class *>(typeDefinition_);
}

Protocol* Type::protocol() const {
    return static_cast<Protocol *>(typeDefinition_);
}

Enum* Type::eenum() const {
    return static_cast<Enum *>(typeDefinition_);
}

ValueType* Type::valueType() const {
    return static_cast<ValueType *>(typeDefinition_);
}

TypeDefinition* Type::typeDefinition() const  {
    return static_cast<TypeDefinition *>(typeDefinition_);
}

bool Type::canHaveGenericArguments() const {
    return type() == TT_CLASS || type() == TT_PROTOCOL;
}

TypeDefinitionFunctional* Type::typeDefinitionFunctional() const {
    return static_cast<TypeDefinitionFunctional *>(typeDefinition_);
}

Type Type::copyWithoutOptional() const {
    Type type = *this;
    type.optional_ = false;
    return type;
}

Type Type::resolveOnSuperArgumentsAndConstraints(TypeContext typeContext, bool resolveSelf) const {
    TypeDefinitionFunctional *c = typeContext.calleeType().typeDefinitionFunctional();
    Type t = *this;
    bool optional = t.optional();
    
    if (resolveSelf && t.type() == TT_SELF) {
        t = typeContext.calleeType();
    }
    
    auto maxReferenceForSuper = c->numberOfGenericArgumentsWithSuperArguments() - c->numberOfOwnGenericArguments();
    // Try to resolve on the generic arguments to the superclass.
    while (t.type() == TT_REFERENCE && t.reference < maxReferenceForSuper) {
        t = c->superGenericArguments()[t.reference];
    }
    while (t.type() == TT_LOCAL_REFERENCE) {
        t = typeContext.function()->genericArgumentConstraints[t.reference];
    }
    while (t.type() == TT_REFERENCE) {
        t = typeContext.calleeType().typeDefinitionFunctional()->genericArgumentConstraints()[t.reference];
    }
    
    if (optional) t.setOptional();
    return t;
}

Type Type::resolveOn(TypeContext typeContext, bool resolveSelf) const {
    Type t = *this;
    bool optional = t.optional();
    
    if (resolveSelf && t.type() == TT_SELF) {
        t = typeContext.calleeType();
    }
    
    while (t.type() == TT_LOCAL_REFERENCE && typeContext.functionGenericArguments()) {
        t = (*typeContext.functionGenericArguments())[t.reference];
    }
    
    if (typeContext.calleeType().canHaveGenericArguments()) {
        while (t.type() == TT_REFERENCE &&
               typeContext.calleeType().typeDefinitionFunctional()->canBeUsedToResolve(t.resolutionConstraint)) {
            Type tn = typeContext.calleeType().genericArguments[t.reference];
            if (tn.type() == TT_REFERENCE && tn.reference == t.reference) {
                break;
            }
            t = tn;
        }
    }
    
    if (optional) t.setOptional();
    
    if (t.canHaveGenericArguments()) {
        for (int i = 0; i < t.typeDefinitionFunctional()->numberOfGenericArgumentsWithSuperArguments(); i++) {
            t.genericArguments[i] = t.genericArguments[i].resolveOn(typeContext);
        }
    }
    else if (t.type() == TT_CALLABLE) {
        for (int i = 0; i < t.arguments + 1; i++) {
            t.genericArguments[i] = t.genericArguments[i].resolveOn(typeContext);
        }
    }
    
    return t;
}

bool Type::identicalGenericArguments(Type to, TypeContext ct, std::vector<CommonTypeFinder> *ctargs) const {
    if (to.typeDefinitionFunctional()->numberOfOwnGenericArguments()) {
        for (int l = to.typeDefinitionFunctional()->numberOfOwnGenericArguments(), i = to.typeDefinitionFunctional()->numberOfGenericArgumentsWithSuperArguments() - l; i < l; i++) {
            if (!this->genericArguments[i].identicalTo(to.genericArguments[i], ct, ctargs)) {
                return false;
            }
        }
    }
    return true;
}

bool Type::compatibleTo(Type to, TypeContext ct, std::vector<CommonTypeFinder> *ctargs) const {
    //(to.optional || !a.optional): Either `to` accepts optionals, or if `to` does not accept optionals `a` mustn't be one.
    if (to.type() == TT_SOMETHING) {
        return true;
    }
    else if (to.type() == TT_SOMEOBJECT && (this->type() == TT_CLASS ||
                                            this->type() == TT_PROTOCOL || this->type() == TT_SOMEOBJECT)) {
        return to.optional() || !this->optional();
    }
    else if (this->type() == TT_CLASS && to.type() == TT_CLASS) {
        return (to.optional() || !this->optional()) && this->eclass()->inheritsFrom(to.eclass())
                && identicalGenericArguments(to, ct, ctargs);
    }
    else if ((this->type() == TT_PROTOCOL && to.type() == TT_PROTOCOL) || (this->type() == TT_VALUE_TYPE && to.type() == TT_VALUE_TYPE)) {
        return (to.optional() || !this->optional()) && this->typeDefinition() == to.typeDefinition()
                && identicalGenericArguments(to, ct, ctargs);
    }
    else if (this->type() == TT_CLASS && to.type() == TT_PROTOCOL) {
        if (to.optional() || !this->optional()) {
            for (Class *a = this->eclass(); a != nullptr; a = a->superclass) {
                for (auto protocol : a->protocols()) {
                    if (protocol.resolveOn(*this).compatibleTo(to, ct, ctargs)) return true;
                }
            }
        }
        return false;
    }
    else if (this->type() == TT_NOTHINGNESS) {
        return to.optional() || to.type() == TT_NOTHINGNESS;
    }
    else if (this->type() == TT_ENUM && to.type() == TT_ENUM) {
        return (to.optional() || !this->optional()) && this->eenum() == to.eenum();
    }
    else if ((this->type() == TT_REFERENCE && to.type() == TT_REFERENCE) ||
             (this->type() == TT_LOCAL_REFERENCE && to.type() == TT_LOCAL_REFERENCE)) {
        if ((to.optional() || !this->optional()) && this->reference == to.reference) {
            return true;
        }
        return (to.optional() || !this->optional())
        && this->resolveOnSuperArgumentsAndConstraints(ct).compatibleTo(to.resolveOnSuperArgumentsAndConstraints(ct), ct, ctargs);
    }
    else if (this->type() == TT_REFERENCE) {
        return (to.optional() || !this->optional()) && this->resolveOnSuperArgumentsAndConstraints(ct).compatibleTo(to, ct, ctargs);
    }
    else if (to.type() == TT_REFERENCE) {
        return (to.optional() || !this->optional()) && this->compatibleTo(to.resolveOnSuperArgumentsAndConstraints(ct), ct, ctargs);
    }
    else if (this->type() == TT_LOCAL_REFERENCE) {
        return ctargs || ((to.optional() || !this->optional()) && this->resolveOnSuperArgumentsAndConstraints(ct).compatibleTo(to, ct, ctargs));
    }
    else if (to.type() == TT_LOCAL_REFERENCE) {
        if (ctargs) {
            (*ctargs)[to.reference].addType(*this, ct);
            return true;
        }
        else {
            return (to.optional() || !this->optional()) && this->compatibleTo(to.resolveOnSuperArgumentsAndConstraints(ct), ct, ctargs);
        }
    }
    else if (to.type() == TT_SELF) {
        return (to.optional() || !this->optional()) && this->compatibleTo(to.resolveOnSuperArgumentsAndConstraints(ct), ct, ctargs);
    }
    else if (this->type() == TT_SELF) {
        return (to.optional() || !this->optional()) && this->resolveOnSuperArgumentsAndConstraints(ct).compatibleTo(to, ct, ctargs);
    }
    else if (this->type() == TT_CALLABLE && to.type() == TT_CALLABLE) {
        if (this->genericArguments[0].compatibleTo(to.genericArguments[0], ct, ctargs) && to.arguments == this->arguments) {
            for (int i = 1; i <= to.arguments; i++) {
                if (!to.genericArguments[i].compatibleTo(this->genericArguments[i], ct, ctargs)) {
                    return false;
                }
            }
            return true;
        }
        return false;
    }
    else {
        return (to.optional() || !this->optional()) && this->type() == to.type();
    }
    return false;
}

bool Type::identicalTo(Type to, TypeContext tc, std::vector<CommonTypeFinder> *ctargs) const {
    if (ctargs && to.type() == TT_LOCAL_REFERENCE) {
        (*ctargs)[to.reference].addType(*this, tc);
        return true;
    }
    
    if (type() == to.type()) {
        switch (type()) {
            case TT_CLASS:
            case TT_PROTOCOL:
            case TT_VALUE_TYPE:
                return typeDefinitionFunctional() == to.typeDefinitionFunctional()
                        && identicalGenericArguments(to, tc, ctargs);
            case TT_CALLABLE:
                return to.arguments == this->arguments && identicalGenericArguments(to, tc, ctargs);
            case TT_ENUM:
                return eenum() == to.eenum();
            case TT_REFERENCE:
            case TT_LOCAL_REFERENCE:
                return reference == to.reference;
            case TT_SELF:
            case TT_SOMETHING:
            case TT_SOMEOBJECT:
            case TT_NOTHINGNESS:
                return true;
        }
    }
    return false;
}

//MARK: Type Visulisation

const char* Type::typePackage() {
    switch (this->type()) {
        case TT_CLASS:
        case TT_VALUE_TYPE:
        case TT_PROTOCOL:
        case TT_ENUM:
            return this->typeDefinition()->package()->name();
        case TT_NOTHINGNESS:
        case TT_SOMETHING:
        case TT_SOMEOBJECT:
        case TT_REFERENCE:
        case TT_LOCAL_REFERENCE:
        case TT_CALLABLE:
        case TT_SELF:
            return "";
    }
}

void stringAppendEc(EmojicodeChar c, std::string &string) {
    ecCharToCharStack(c, sc);
    string.append(sc);
}

void Type::typeName(Type type, TypeContext typeContext, bool includePackageAndOptional, std::string &string) const {
    if (includePackageAndOptional) {
        if (type.optional()) {
            stringAppendEc(E_CANDY, string);
        }
        
        string.append(type.typePackage());
    }
    
    switch (type.type()) {
        case TT_CLASS:
        case TT_PROTOCOL:
        case TT_ENUM:
        case TT_VALUE_TYPE:
            stringAppendEc(type.typeDefinition()->name(), string);
            break;
        case TT_NOTHINGNESS:
            stringAppendEc(E_SPARKLES, string);
            return;
        case TT_SOMETHING:
            stringAppendEc(E_MEDIUM_WHITE_CIRCLE, string);
            return;
        case TT_SOMEOBJECT:
            stringAppendEc(E_LARGE_BLUE_CIRCLE, string);
            return;
        case TT_SELF:
            stringAppendEc(E_ROOTSTER, string);
            return;
        case TT_CALLABLE:
            stringAppendEc(E_GRAPES, string);
            
            for (int i = 1; i <= type.arguments; i++) {
                typeName(type.genericArguments[i], typeContext, includePackageAndOptional, string);
            }
            
            stringAppendEc(E_RIGHTWARDS_ARROW, string);
            stringAppendEc(0xFE0F, string);
            typeName(type.genericArguments[0], typeContext, includePackageAndOptional, string);
            stringAppendEc(E_WATERMELON, string);
            return;
        case TT_REFERENCE: {
            if (typeContext.calleeType().type() == TT_CLASS) {
                Class *eclass = typeContext.calleeType().eclass();
                do {
                    for (auto it : eclass->ownGenericArgumentVariables()) {
                        if (it.second.reference == type.reference) {
                            string.append(it.first.utf8CString());
                            return;
                        }
                    }
                } while ((eclass = eclass->superclass));
            }
            else if (typeContext.calleeType().canHaveGenericArguments()) {
                for (auto it : typeContext.calleeType().typeDefinitionFunctional()->ownGenericArgumentVariables()) {
                    if (it.second.reference == type.reference) {
                        string.append(it.first.utf8CString());
                        return;
                    }
                }
            }
            
            stringAppendEc('T', string);
            stringAppendEc('0' + type.reference, string);
            return;
        }
        case TT_LOCAL_REFERENCE:
            if (typeContext.function()) {
                for (auto it : typeContext.function()->genericArgumentVariables) {
                    if (it.second.reference == type.reference) {
                        string.append(it.first.utf8CString());
                        return;
                    }
                }
            }
            
            stringAppendEc('L', string);
            stringAppendEc('0' + type.reference, string);
            return;
    }
    
    if (typeContext.calleeType().type() != TT_NOTHINGNESS && type.canHaveGenericArguments()) {
        auto typeDef = type.typeDefinitionFunctional();
        int offset = typeDef->numberOfGenericArgumentsWithSuperArguments() - typeDef->numberOfOwnGenericArguments();
        for (int i = 0, l = typeDef->numberOfOwnGenericArguments(); i < l; i++) {
            stringAppendEc(E_SPIRAL_SHELL, string);
            typeName(type.genericArguments[offset + i], typeContext, includePackageAndOptional, string);
        }
    }
}

std::string Type::toString(TypeContext typeContext, bool includeNsAndOptional) const {
    std::string string;
    typeName(*this, typeContext, includeNsAndOptional, string);
    return string;
}
