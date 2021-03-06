#include "classdef-table-view.h"

using namespace Yuni;


namespace ny {


void ClassdefTableView::mergeSubstitutes() {
	table.mergeSubstitutes();
}


void ClassdefTableView::printClassdef(Yuni::String& out, const CLID& clid, const Classdef& cdef) const {
	out.write("    ", 4);
	out << clid;
	if (cdef.clid != clid) {
		out << " -> " << cdef.clid;
		out << ": ";
		cdef.print(out, *this, false);
		out << '\n';
	}
	else {
		out << ": ";
		cdef.print(out, *this, false);
		out << '\n';
		if (cdef.qualifiers.ref)
			out << "        +ref\n";
		if (cdef.qualifiers.constant)
			out << "        +constant\n";
		if (not cdef.parentclid.isVoid())
			out << "        parent: " << cdef.parentclid << '\n';
		cdef.interface.print(out, false);
		cdef.followup.print(out, false);
	}
}


void ClassdefTableView::print(Yuni::String& out, bool clearBefore) const {
	if (clearBefore)
		out.clear();
	out << "CLASSDEF TABLE\n";
	std::map<uint32_t, std::map<uint32_t, Classdef*>> orderedlist;
	for (auto& pair : table.m_classdefs)
		orderedlist[pair.first.atomid()][pair.first.lvid()] = Ref<Classdef>::WeakPointer(pair.second);
	bool firstAtom = true;
	for (auto& pairAtomID : orderedlist) {
		if (not firstAtom)
			out << '\n';
		out << "    ----------------------------------------------------[ atom " << pairAtomID.first << " ]---\n";
		firstAtom = false;
		for (auto& pairuint32_t : pairAtomID.second) {
			CLID clid{pairAtomID.first, pairuint32_t.first};
			printClassdef(out, clid, *(pairuint32_t.second));
		}
	}
}


} // namespace ny
