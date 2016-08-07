#include "project.h"
#include "details/vm/runtime/std.core.h"
#include "libnanyc-config.h"

using namespace Yuni;




namespace Nany
{

	void Project::init()
	{
		targets.anonym = new CTarget(self(), "{default}");
		targets.nsl    = new CTarget(self(), "{nsl}");

		if (Config::importNSL)
			importNSLCore(*this);

		if (cf.on_create)
			cf.on_create(self());
	}


	void Project::destroy()
	{
		if (cf.on_destroy)
			cf.on_destroy(self());

		this->~Project();

		auto& allocator = cf.allocator;
		allocator.deallocate(&allocator, this, sizeof(Nany::Project));
	}



} // namespace Nany
