#pragma once
#include <yuni/yuni.h>
#include <yuni/string.h>
#include <yuni/core/smartptr/intrusive.h>
#include "nany/nany.h"
#include "libnanyc-config.h"
#include <array>




namespace Nany
{

	/*!
	** \brief Definition of a single user-defined intrinsic
	*/
	class Intrinsic final
		: public Yuni::IIntrusiveSmartPtr<Intrinsic, false, Yuni::Policy::SingleThreaded>
		, Yuni::NonCopyable<Intrinsic>
	{
	public:
		//! The class ancestor
		typedef Yuni::IIntrusiveSmartPtr<Intrinsic, false, Yuni::Policy::SingleThreaded>  Ancestor;
		//! The most suitable smart ptr for the class
		typedef Ancestor::SmartPtrType<Intrinsic>::Ptr  Ptr;


	public:
		Intrinsic(const AnyString& name, void* callback)
			: callback(callback)
			, name(name)
		{}

		Intrinsic(const Intrinsic&) = default;

		YString print() const;

	public:
		//! C-Callback
		void* callback;

		//! name of the intrinsic
		const Yuni::ShortString64 name;

		//! The return type
		nytype_t rettype = nyt_void;
		//! The total number of parameters
		uint32_t paramcount = 0;
		//! All parameter types
		std::array<nytype_t, Config::maxPushedParameters> params;

		//! Intrinsic ID
		uint32_t id = (uint32_t) -1;

	}; // class Intrinsic




} // namespace Nany