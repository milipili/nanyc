#include "mapping.h"
#include "details/atom/classdef-table-view.h"
#include "details/ir/sequence.h"

using namespace Yuni;


namespace ny {
namespace Pass {
namespace Mapping {


namespace {


//! Stack frame per atom definition (class, func)
struct AtomStackFrame final {
	AtomStackFrame(Atom& atom, std::unique_ptr<AtomStackFrame>& next)
		: atom(atom), next(std::move(next)) {
	}
	//! The current atom
	Atom& atom;
	//! The current scope depth for the current stack frame
	uint32_t scope = 0u;
	uint32_t parameterIndex = 0u;
	//! Convenient classdefs alias
	std::vector<CLID> classdefs;
	// Next frame
	std::unique_ptr<AtomStackFrame> next;

	//! Information for capturing variables
	struct CaptureVariables {
		//! Get if allowed to capture variables
		bool enabled() const {
			return atom != nullptr;
		}

		void enabled(Atom& newatom) {
			atom = &newatom;
			atom->flags += Atom::Flags::captureVariables;
			if (!newatom.candidatesForCapture) {
				typedef decltype(newatom.candidatesForCapture) Set;
				newatom.candidatesForCapture = std::make_unique<Set::element_type>();
			}
		}

		//! Try to list of all unknown identifiers, potential candidates for capture
		Atom* atom = nullptr;
		//! all local named variables (name / scope)
		std::unordered_map<AnyString, uint32_t> knownVars;
	}
	capture;

	Atom& currentAtomNotUnit() {
		return (not atom.isUnit()) ? atom : (*(atom.parent));
	}

}; // class AtomStackFrame


struct OpcodeReader final {
	OpcodeReader(SequenceMapping& mapping)
		: cdeftable(mapping.cdeftable)
		, mutex(mapping.mutex)
		, currentSequence(mapping.currentSequence)
		, firstAtomCreated(mapping.firstAtomCreated)
		, prefixNameForFirstAtomCreated{mapping.prefixNameForFirstAtomCreated}
		, evaluateWholeSequence(mapping.evaluateWholeSequence)
		, mapping(mapping) {
		firstAtomCreated = nullptr;
		lastPushedNamedParameters.reserve(8); // arbitrary
		lastPushedIndexedParameters.reserve(8);
	}


	void complainOperand(const ir::Instruction& operands, AnyString msg) {
		// example: ICE: unknown opcode 'resolveAttribute': from 'ref %4 = resolve %3."()"'
		auto trace = ice();
		if (not msg.empty())
			trace << msg << ':';
		else
			trace << "invalid opcode ";
		trace << " '" << ir::ISA::print(currentSequence, operands) << '\'';
		success = false;
	}


	template<ir::ISA::Op O>
	void complainOperand(const ir::ISA::Operand<O>& operands, AnyString msg) {
		complainOperand(ir::Instruction::fromOpcode(operands), msg);
	}


	template<ir::ISA::Op O>
	bool checkForuint32_t(const ir::ISA::Operand<O>& operands, uint32_t lvid) {
		if (debugmode) {
			if (unlikely(lvid == 0 or not (lvid < atomStack->classdefs.size()))) {
				complainOperand(operands, String{"mapping: invalid lvid %"} << lvid
								<< " (upper bound: %" << atomStack->classdefs.size() << ')');
				return false;
			}
		}
		return true;
	}


	void pushNewFrame(Atom& atom) {
		std::unique_ptr<AtomStackFrame> next{atomStack.release()};
		atomStack = std::make_unique<AtomStackFrame>(atom, next);
	}


	void resetClassdefOriginFromCurrentPosition(Classdef& cdef) {
		cdef.origins.filename = currentFilename;
		cdef.origins.line     = currentLine;
		cdef.origins.offset   = currentOffset;
	}


	void attachFuncCall(const ir::ISA::Operand<ir::ISA::Op::call>& operands) {
		if (unlikely(not checkForuint32_t(operands, operands.ptr2func)))
			return;
		if (unlikely(not checkForuint32_t(operands, operands.lvid)))
			return;
		auto& stackframe     = *atomStack;
		auto  atomid         = stackframe.atom.atomid;
		auto& clidFuncToCall = stackframe.classdefs[operands.ptr2func];
		auto& clidRetValue   = stackframe.classdefs[operands.lvid];
		// update all underlying types
		{
			CLID clid{atomid, 0};
			MutexLocker locker{mutex};
			resetClassdefOriginFromCurrentPosition(cdeftable.classdef(clidRetValue));
			auto& funcdef = cdeftable.addClassdefInterfaceSelf(clidFuncToCall);
			funcdef.resetReturnType(clidRetValue);
			funcdef.clid = clidFuncToCall;
			for (uint i = 0; i != lastPushedIndexedParameters.size(); ++i) {
				auto lvid = lastPushedIndexedParameters[i];
				clid.reclass(lvid);
				funcdef.appendParameter(clid);
				auto& followup = cdeftable.classdef(clid).followup;
				followup.pushedIndexedParams.push_back(std::make_pair(clidFuncToCall, i));
			}
			for (auto& pair : lastPushedNamedParameters) {
				clid.reclass(pair.second);
				funcdef.appendParameter(pair.first, clid);
				auto& followup = cdeftable.classdef(clid).followup;
				followup.pushedNamedParams.push_back(std::make_pair(clidFuncToCall, pair.first));
			}
		}
	}


	void mapBlueprintFuncdefOrTypedef(ir::ISA::Operand<ir::ISA::Op::blueprint>& operands) {
		// functions and typedef are instanciated the sameway (with some minor differences)
		auto kind = static_cast<ir::ISA::Blueprint>(operands.kind);
		bool isFuncdef = (kind == ir::ISA::Blueprint::funcdef);
		// registering the blueprint into the outline...
		Atom& atom = atomStack->currentAtomNotUnit();
		AnyString funcname = currentSequence.stringrefs[operands.name];
		if (unlikely(funcname.empty()))
			return complainOperand(operands, (isFuncdef) ? "invalid func name" : "invalid typedef name");
		// reset last lvid and parameters
		lastuint32_t = 0;
		lastPushedNamedParameters.clear();
		lastPushedIndexedParameters.clear();
		// global func operators (or unittest) always belong to root, even if declared in a specific namespace
		bool isGlobalOperator = isFuncdef and atom.type == Atom::Type::namespacedef
								and funcname[0] == '^'
								and (not funcname.startsWith("^view^"))
								and (not funcname.startsWith("^prop"));
		auto& parentAtom = (not isGlobalOperator)
						   ? atom
						   : cdeftable.atoms.root;
		MutexLocker locker{mutex};
		auto& newatom = isFuncdef
						? cdeftable.atoms.createFuncdef(parentAtom, funcname)
						: cdeftable.atoms.createTypealias(parentAtom, funcname);
		newatom.opcodes.sequence = &currentSequence;
		newatom.opcodes.offset   = currentSequence.offsetOf(operands);
		cdeftable.registerAtom(newatom);
		operands.atomid = newatom.atomid;
		newatom.returnType.clid.reclass(newatom.atomid, 1);
		if (parentAtom.atomid != atom.atomid)
			newatom.scopeForNameResolution = &atom;
		needAtomDbgFileReport = true;
		needAtomDbgOffsetReport = true;
		pushNewFrame(newatom);
		// capture unknown variables ?
		if (not isGlobalOperator) {
			if (newatom.isClassMember() and newatom.parent->flags(Atom::Flags::captureVariables)) {
				atomStack->capture.enabled(*newatom.parent);
				newatom.flags += Atom::Flags::captureVariables;
			}
		}
		if (!firstAtomCreated)
			firstAtomCreated = &newatom;
	}


	void mapBlueprintClassdef(ir::ISA::Operand<ir::ISA::Op::blueprint>& operands) {
		// registering the blueprint into the outline...
		Atom& atom = atomStack->currentAtomNotUnit();
		// reset last lvid and parameters
		lastuint32_t = 0;
		lastPushedNamedParameters.clear();
		lastPushedIndexedParameters.clear();
		AnyString classname = currentSequence.stringrefs[operands.name];
		Atom& newClassAtom = [&]() -> Atom& {
			if (prefixNameForFirstAtomCreated.empty()) {
				MutexLocker locker{mutex};
				auto& newClassAtom = cdeftable.atoms.createClassdef(atom, classname);
				cdeftable.registerAtom(newClassAtom);
				return newClassAtom;
			}
			else
			{
				String tmpname;
				tmpname.reserve(classname.size() + prefixNameForFirstAtomCreated.size());
				tmpname << prefixNameForFirstAtomCreated << classname;
				prefixNameForFirstAtomCreated.clear();
				MutexLocker locker{mutex};
				auto& newClassAtom = cdeftable.atoms.createClassdef(atom, tmpname);
				cdeftable.registerAtom(newClassAtom);
				return newClassAtom;
			}
		}();
		newClassAtom.opcodes.sequence = &currentSequence;
		newClassAtom.opcodes.offset   = currentSequence.offsetOf(operands);
		// update atomid
		operands.atomid = newClassAtom.atomid;
		// requires additional information
		needAtomDbgFileReport = true;
		needAtomDbgOffsetReport = true;
		pushNewFrame(newClassAtom);
		if (operands.lvid != 0)
			atomStack->capture.enabled(newClassAtom);
		if (!firstAtomCreated)
			firstAtomCreated = &newClassAtom;
	}


	void mapBlueprintParam(ir::ISA::Operand<ir::ISA::Op::blueprint>& operands) {
		auto& frame = *atomStack;
		// calculating the lvid for the current parameter
		// (+1 since %1 is the return value/type)
		uint paramuint32_t = (++frame.parameterIndex) + 1;
		if (unlikely(not checkForuint32_t(operands, paramuint32_t)))
			return;
		auto kind = static_cast<ir::ISA::Blueprint>(operands.kind);
		assert(kind == ir::ISA::Blueprint::param or kind == ir::ISA::Blueprint::paramself
			   or kind == ir::ISA::Blueprint::gentypeparam);
		bool isGenTypeParam = (kind == ir::ISA::Blueprint::gentypeparam);
		if (unlikely(not isGenTypeParam and not frame.atom.isFunction()))
			return complainOperand(operands, "parameter for non-function");
		CLID clid{frame.atom.atomid, paramuint32_t};
		AnyString name = currentSequence.stringrefs[operands.name];
		auto& parameters = (not isGenTypeParam)
						   ? frame.atom.parameters : frame.atom.tmplparams;
		parameters.append(clid, name);
		if (frame.capture.enabled())
			frame.capture.knownVars[name] = frame.scope;
		MutexLocker locker{mutex};
		// information about the parameter itself
		auto& cdef = cdeftable.classdef(clid);
		cdef.instance = not isGenTypeParam;
		cdef.qualifiers.ref = false; // should not be 'ref' by default, contrary to all other classdefs
		// making sure the classdef is 'any'
		assert(cdef.atom == nullptr and cdef.isAny());
		if (isGenTypeParam) {
			// if a generic type parameter, generating an implicit typedef
			Atom& atom = atomStack->currentAtomNotUnit();
			auto pindex = atom.childrenCount(); // children are currently only typedefs from generic params
			auto& newAliasAtom = cdeftable.atoms.createTypealias(atom, name);
			newAliasAtom.classinfo.isInstanciated = true;
			cdeftable.registerAtom(newAliasAtom);
			newAliasAtom.returnType.clid = cdef.clid; // type of the typedef
			newAliasAtom.classinfo.nextFieldIndex = static_cast<uint16_t>(pindex);
		}
	}


	void mapBlueprintVardef(ir::ISA::Operand<ir::ISA::Op::blueprint>& operands) {
		// registering the blueprint into the outline...
		Atom& atom = atomStack->currentAtomNotUnit();
		AnyString varname = currentSequence.stringrefs[operands.name];
		if (unlikely(varname.empty()))
			return complainOperand(operands, "invalid func name");
		if (unlikely(atom.type != Atom::Type::classdef))
			return complainOperand(operands, "vardef: invalid parent atom");
		if (atomStack->capture.enabled())
			atomStack->capture.knownVars[varname] = atomStack->scope;
		MutexLocker locker{mutex};
		auto& newVarAtom = cdeftable.atoms.createVardef(atom, varname);
		cdeftable.registerAtom(newVarAtom);
		newVarAtom.returnType.clid.reclass(atom.atomid, operands.lvid);
		if (!firstAtomCreated)
			firstAtomCreated = &newVarAtom;
	}


	void mapBlueprintNamespace(ir::ISA::Operand<ir::ISA::Op::blueprint>& operands) {
		AnyString nmname = currentSequence.stringrefs[operands.name];
		Atom& parentAtom = atomStack->currentAtomNotUnit();
		MutexLocker locker{mutex};
		Atom& newRoot = cdeftable.atoms.createNamespace(parentAtom, nmname);
		cdeftable.registerAtom(newRoot);
		pushNewFrame(newRoot);
	}


	void mapBlueprintUnit(ir::ISA::Operand<ir::ISA::Op::blueprint>& operands) {
		Atom& parentAtom = atomStack->currentAtomNotUnit();
		MutexLocker locker{mutex};
		auto& newRoot = cdeftable.atoms.createUnit(parentAtom, currentFilename);
		cdeftable.registerAtom(newRoot);
		operands.atomid = newRoot.atomid;
		assert(newRoot.atomid != 0);
		pushNewFrame(newRoot);
	}


	void visit(ir::ISA::Operand<ir::ISA::Op::blueprint>& operands) {
		if (unlikely(nullptr == atomStack))
			return complainOperand(operands, "invalid stack for blueprint");
		auto kind = static_cast<ir::ISA::Blueprint>(operands.kind);
		switch (kind) {
			case ir::ISA::Blueprint::vardef: {
				mapBlueprintVardef(operands);
				break;
			}
			case ir::ISA::Blueprint::param:
			case ir::ISA::Blueprint::paramself:
			case ir::ISA::Blueprint::gentypeparam: {
				mapBlueprintParam(operands);
				break;
			}
			case ir::ISA::Blueprint::funcdef:
			case ir::ISA::Blueprint::typealias: {
				mapBlueprintFuncdefOrTypedef(operands);
				break;
			}
			case ir::ISA::Blueprint::classdef: {
				mapBlueprintClassdef(operands);
				break;
			}
			case ir::ISA::Blueprint::namespacedef: {
				mapBlueprintNamespace(operands);
				break;
			}
			case ir::ISA::Blueprint::unit: {
				mapBlueprintUnit(operands);
				break;
			}
		}
	}


	void visit(ir::ISA::Operand<ir::ISA::Op::pragma>& operands) {
		if (unlikely(nullptr == atomStack))
			return complainOperand(operands, "invalid stack for blueprint pragma");
		switch (operands.pragma) {
			case ir::ISA::Pragma::codegen: {
				break;
			}
			case ir::ISA::Pragma::builtinalias: {
				Atom& atom = atomStack->atom;
				atom.builtinalias = currentSequence.stringrefs[operands.value.builtinalias.namesid];
				break;
			}
			case ir::ISA::Pragma::shortcircuit: {
				bool onoff = (0 != operands.value.shortcircuit);
				atomStack->atom.parameters.shortcircuitValue = onoff;
				break;
			}
			case ir::ISA::Pragma::suggest: {
				bool onoff = (0 != operands.value.suggest);
				if (not onoff)
					atomStack->atom.flags -= Atom::Flags::suggestInReport;
				else
					atomStack->atom.flags += Atom::Flags::suggestInReport;
				break;
			}
			case ir::ISA::Pragma::synthetic:
			case ir::ISA::Pragma::blueprintsize:
			case ir::ISA::Pragma::visibility:
			case ir::ISA::Pragma::bodystart:
			case ir::ISA::Pragma::shortcircuitOpNopOffset:
			case ir::ISA::Pragma::shortcircuitMutateToBool:
			case ir::ISA::Pragma::unknown:
				break;
		}
	}


	void visit(ir::ISA::Operand<ir::ISA::Op::stacksize>& operands) {
		if (unlikely(nullptr == atomStack))
			return complainOperand(operands, "invalid parent atom");
		Atom& parentAtom = atomStack->atom;
		if (unlikely(parentAtom.atomid == 0))
			return complainOperand(operands, "mapping: invalid parent atom id");
		// creating all related classdefs
		// (take max with 1 to prevent against invalid opcode)
		uint localvarCount = operands.add;
		MutexLocker locker{mutex};
		parentAtom.localVariablesCount = localvarCount;
		cdeftable.bulkCreate(atomStack->classdefs, parentAtom.atomid, localvarCount);
		switch (parentAtom.type) {
			case Atom::Type::funcdef:
			case Atom::Type::typealias: {
				// creating all related classdefs
				// (take max with 1 to prevent against invalid opcode)
				if (unlikely(localvarCount == 0))
					return complainOperand(operands, "invalid local variable count for a func blueprint");
				// like parameters, the return type should not 'ref' by default
				cdeftable.classdef(CLID{parentAtom.atomid, 1}).qualifiers.ref = false;
				break;
			}
			default:
				break;
		}
	}


	void visit(ir::ISA::Operand<ir::ISA::Op::scope>& operands) {
		if (unlikely(nullptr == atomStack))
			return complainOperand(operands, "invalid stack");
		++(atomStack->scope);
		lastuint32_t = 0;
	}


	void visit(ir::ISA::Operand<ir::ISA::Op::end>&) {
		// reset the last lvid
		lastuint32_t = 0u;
		if (likely(atomStack)) {
			auto& scope = atomStack->scope;
			// the scope might be zero if the opcode 'end' comes from a class or a func
			if (scope > 0) {
				--scope;
				if (atomStack->capture.enabled()) {
					auto& set = atomStack->capture.knownVars;
					for (auto it = set.begin(); it != set.end(); ) {
						if (it->second > scope)
							it = set.erase(it);
						else
							++it;
					}
				}
			}
			else {
				// pop the stack frame
				std::unique_ptr<AtomStackFrame> next{atomStack->next.release()};
				atomStack.swap(next);
				if (!atomStack or (not evaluateWholeSequence and !atomStack->next))
					currentSequence.invalidateCursor(*cursor);
			}
		}
		else {
			assert(false and "invalid stack");
			currentSequence.invalidateCursor(*cursor);
		}
	}


	void visit(ir::ISA::Operand<ir::ISA::Op::stackalloc>& operands) {
		if (unlikely(not checkForuint32_t(operands, operands.lvid)))
			return;
		lastuint32_t = operands.lvid;
		auto clid = atomStack->classdefs[operands.lvid];
		MutexLocker locker{mutex};
		auto& cdef = cdeftable.classdef(clid);
		resetClassdefOriginFromCurrentPosition(cdef);
		switch ((nytype_t) operands.type) {
			default: {
				cdef.mutateToBuiltin((nytype_t) operands.type);
				cdef.instance = true; // keep somewhere that this definition is a variable instance
				break;
			}
			case nyt_void:
				cdef.mutateToVoid();
				break;
			case nyt_any:
				cdef.mutateToAny();
				break;
		}
	}


	void visit(ir::ISA::Operand<ir::ISA::Op::self>& operands) {
		if (unlikely(not checkForuint32_t(operands, operands.self)))
			return;
		if (unlikely(nullptr == atomStack))
			return complainOperand(operands, "invalid atom stack for 'resolveAsSelf'");
		// we can have at least 2 patterns:
		//
		//  * the most frequent, called from a method contained within a class
		//  * from the class itself, most likely a variable
		// clid of the target variable
		CLID clid {atomStack->atom.atomid, operands.self};
		for (auto* rit = atomStack.get(); rit; rit = rit->next.get()) {
			if (rit->atom.isClass()) {
				MutexLocker locker{mutex};
				auto& cdef = cdeftable.classdef(clid);
				resetClassdefOriginFromCurrentPosition(cdef);
				cdef.mutateToAtom(&(rit->atom));
				cdef.qualifiers.ref = true;
				return;
			}
		}
		// fallback - complainOperand
		complainOperand(operands, "failed to find parent class for 'resolveAsSelf'");
	}


	void visit(ir::ISA::Operand<ir::ISA::Op::identify>& operands) {
		if (unlikely(not checkForuint32_t(operands, operands.lvid)))
			return;
		if (unlikely(operands.text == 0))
			return complainOperand(operands, "invalid symbol name");
		AnyString name = currentSequence.stringrefs[operands.text];
		if (unlikely(name.empty()))
			return complainOperand(operands, "invalid empty identifier");
		lastuint32_t = operands.lvid;
		auto& atomFrame = *atomStack;
		auto& localClassdefs = atomFrame.classdefs;
		assert(operands.lvid < localClassdefs.size());
		auto& clid = localClassdefs[operands.lvid];
		if (operands.self == 0) {
			// try to determine whether a new variable should be captured
			if (atomFrame.capture.enabled()) {
				if (name[0] != '^' and atomFrame.capture.knownVars.count(name) == 0) {
					// not 100% sure, but this unknown name might be a variable to capture
					Atom& atm = *atomFrame.capture.atom;
					atm.candidatesForCapture->insert(name);
					atomFrame.capture.knownVars[name] = 0; // no scope
				}
			}
			MutexLocker locker{mutex};
			// will see later - currently unknown
			// classdef.mutateToAny();
			cdeftable.addClassdefInterfaceSelf(clid, name);
			resetClassdefOriginFromCurrentPosition(cdeftable.classdef(clid));
		}
		else {
			// trying to resolve an attribute
			// (aka `parent.current`)
			if (not checkForuint32_t(operands, operands.self))
				return;
			// the type is currently unknown
			// classdef.mutateToAny();
			// retrieving the parent classdef
			assert(operands.self < localClassdefs.size());
			auto& selfClassdef = localClassdefs[operands.self];
			MutexLocker locker{mutex};
			auto& funcdef = cdeftable.addClassdefInterface(selfClassdef, name);
			funcdef.clid  = clid;
			auto& cdef = cdeftable.classdef(clid);
			cdef.parentclid = selfClassdef;
			resetClassdefOriginFromCurrentPosition(cdef);
		}
	}


	void visit(ir::ISA::Operand<ir::ISA::Op::identifyset>& operands) {
		auto& newopc = ir::Instruction::fromOpcode(operands).to<ir::ISA::Op::identify>();
		visit(newopc);
	}


	void visit(ir::ISA::Operand<ir::ISA::Op::tpush>& operands) {
		if (unlikely(not checkForuint32_t(operands, operands.lvid)))
			return;
	}


	void visit(ir::ISA::Operand<ir::ISA::Op::push>& operands) {
		if (unlikely(not checkForuint32_t(operands, operands.lvid)))
			return;
		if (operands.name != 0) { // named parameter
			AnyString name = currentSequence.stringrefs[operands.name];
			lastPushedNamedParameters.emplace_back(std::make_pair(name, operands.lvid));
		}
		else {
			if (unlikely(not lastPushedNamedParameters.empty()))
				return complainOperand(operands, "named parameters must be provided after standard parameters");
			lastPushedIndexedParameters.emplace_back(operands.lvid);
		}
	}


	void visit(ir::ISA::Operand<ir::ISA::Op::call>& operands) {
		attachFuncCall(operands);
		lastuint32_t = operands.lvid;
		lastPushedIndexedParameters.clear();
		lastPushedNamedParameters.clear();
	}


	void visit(ir::ISA::Operand<ir::ISA::Op::ret>& operands) {
		if (operands.lvid != 0) {
			if (unlikely(not checkForuint32_t(operands, operands.lvid)))
				return;
			auto& classdefRetValue = atomStack->classdefs[1]; // 1 is the return value
			auto& classdef = atomStack->classdefs[operands.lvid];
			MutexLocker locker{mutex};
			auto& followup = cdeftable.classdef(classdefRetValue).followup;
			followup.extends.push_back(classdef);
		}
	}


	void visit(ir::ISA::Operand<ir::ISA::Op::follow>& operands) {
		if (unlikely(not checkForuint32_t(operands, operands.lvid)))
			return;
		if (unlikely(not checkForuint32_t(operands, operands.follower)))
			return;
		auto& clidFollower = atomStack->classdefs[operands.follower];
		auto& clid = atomStack->classdefs[operands.lvid];
		MutexLocker locker{mutex};
		if (operands.symlink)
			cdeftable.makeHardlink(clid, clidFollower);
		else
			cdeftable.classdef(clidFollower).followup.extends.push_back(clid);
	}


	void visit(ir::ISA::Operand<ir::ISA::Op::intrinsic>& operands) {
		if (unlikely(not checkForuint32_t(operands, operands.lvid)))
			return;
	}


	void visit(ir::ISA::Operand<ir::ISA::Op::debugfile>& operands) {
		currentFilename = currentSequence.stringrefs[operands.filename].c_str();
		if (needAtomDbgFileReport) {
			needAtomDbgFileReport = false;
			atomStack->atom.origin.filename = currentFilename;
		}
	}


	void visit(ir::ISA::Operand<ir::ISA::Op::debugpos>& operands) {
		currentLine = operands.line;
		currentOffset = operands.offset;
		if (unlikely(needAtomDbgOffsetReport)) {
			needAtomDbgOffsetReport = false;
			atomStack->atom.origin.line = currentLine;
			atomStack->atom.origin.offset = currentOffset;
		}
	}


	void visit(ir::ISA::Operand<ir::ISA::Op::qualifiers>& operands) {
		assert(static_cast<uint32_t>(operands.qualifier) < ir::ISA::TypeQualifierCount);
		CLID clid {atomStack->atom.atomid, operands.lvid};
		bool onoff = (operands.flag != 0);
		MutexLocker locker{mutex};
		if (debugmode and not cdeftable.hasClassdef(clid))
			return complainOperand(operands, "invalid clid");
		auto& qualifiers = cdeftable.classdef(clid).qualifiers;
		switch (operands.qualifier) {
			case ir::ISA::TypeQualifier::ref:
				qualifiers.ref = onoff;
				break;
			case ir::ISA::TypeQualifier::constant:
				qualifiers.constant = onoff;
				break;
		}
	}


	template<ir::ISA::Op O>
	void visit(ir::ISA::Operand<O>& operands) {
		switch (O) {
			// all following opcodes can be safely ignored
			case ir::ISA::Op::allocate:
			case ir::ISA::Op::comment:
			case ir::ISA::Op::ensureresolved:
			case ir::ISA::Op::classdefsizeof:
			case ir::ISA::Op::namealias:
			case ir::ISA::Op::store:
			case ir::ISA::Op::storeText:
			case ir::ISA::Op::storeConstant:
			case ir::ISA::Op::memalloc:
			case ir::ISA::Op::memcopy:
			case ir::ISA::Op::typeisobject:
			case ir::ISA::Op::ref:
			case ir::ISA::Op::unref:
			case ir::ISA::Op::assign:
			case ir::ISA::Op::label:
			case ir::ISA::Op::jmp:
			case ir::ISA::Op::jz:
			case ir::ISA::Op::jnz:
			case ir::ISA::Op::nop:
			case ir::ISA::Op::commontype:
				break;
			// error for all the other ones
			default:
				complainOperand(operands, "unsupported opcode in mapping");
				break;
		}
	}


	bool map(Atom& parentAtom, uint32_t offset) {
		// some reset if reused
		assert(not atomStack);
		pushNewFrame(parentAtom);
		lastuint32_t = 0u;
		lastPushedNamedParameters.clear();
		lastPushedIndexedParameters.clear();
		currentFilename = nullptr;
		currentOffset = 0u;
		currentLine = 0u;
		success = true;
		// -- walk through all opcodes
		currentSequence.each(*this, offset);
		atomStack.reset(nullptr); // cleanup after use
		return success;
	}


	//! The classdef table (must be protected by 'mutex' in some passes)
	ClassdefTable& cdeftable;
	//! Mutex for the cdeftable
	Yuni::Mutex& mutex;
	//! Current sequence
	ir::Sequence& currentSequence;

	//! Blueprint root element
	std::unique_ptr<AtomStackFrame> atomStack;
	//! Last lvid (for pushed parameters)
	uint32_t lastuint32_t = 0;
	//! The first atom created by the mapping
	Atom*& firstAtomCreated;
	//! Last pushed indexed parameters
	std::vector<uint32_t> lastPushedIndexedParameters;
	//! Last pushed named parameters
	std::vector<std::pair<AnyString, uint32_t>> lastPushedNamedParameters;
	//! Prefix to prepend for the first atom created by the mapping
	AnyString prefixNameForFirstAtomCreated;
	//! Flag to evaluate the whole sequence, or only a portion of it
	bool evaluateWholeSequence = true;

	bool needAtomDbgFileReport = false;
	bool needAtomDbgOffsetReport = false;

	//! cursor for iterating through all opcocdes
	ir::Instruction** cursor = nullptr;

	const char* currentFilename = nullptr;
	uint32_t currentLine = 0;
	uint32_t currentOffset = 0;
	SequenceMapping& mapping;
	bool success = false;
};


} // anonymous namespace


SequenceMapping::SequenceMapping(ClassdefTable& cdeftable, Mutex& mutex, ir::Sequence& sequence)
	: cdeftable(cdeftable)
	, mutex(mutex)
	, currentSequence(sequence) {
}


static void retriveReportMetadata(void* self, Logs::Level level, const AST::Node*, String& filename,
		uint32_t& line, uint32_t& offset) {
	auto& sb    = *(reinterpret_cast<OpcodeReader*>(self));
	sb.success &= Logs::isError(level);
	filename    = sb.currentFilename;
	line        = sb.currentLine;
	offset      = sb.currentOffset;
}


bool SequenceMapping::map(Atom& parentAtom, uint32_t offset) {
	OpcodeReader reader{*this};
	Logs::MetadataHandler handler{&reader, &retriveReportMetadata};
	return reader.map(parentAtom, offset);
}


} // namespace Mapping
} // namespace Pass
} // namespace ny