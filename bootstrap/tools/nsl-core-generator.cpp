#include <yuni/yuni.h>
#include <yuni/core/string.h>
#include <yuni/io/file.h>
#include <iostream>

using namespace Yuni;




namespace {


void craftClassFloat(Clob& o, uint32_t bits, const AnyString& license, const AnyString& filename)
{
	o.clear();
	ShortString16 suffix;
	char c = 'f';
	suffix << c << bits;

	o << license;
	o << "/// \\file    " << suffix << ".ny\n";
	o << "/// \\brief   Implementation of the class " << suffix << ", ";
	AnyString name;
	switch (bits)
	{
		case 128: name = "quadruple-precision floating-point number (128bits)"; break;
		case 64:  name = "double-precision floating-point number (64bits)"; break;
		case 32:  name = "single-precision floating-point number (32bits)"; break;
	}
	o << name << '\n';
	o << "/// \\ingroup std.core\n";
	o << "/// \\important THIS FILE IS AUTOMATICALLY GENERATED (see `nsl-core-generator.cpp`)\n";
	o << '\n';
	o << '\n';
	o << '\n';
	o << '\n';
	o << "/// \\brief   " << name << '\n';
	o << "/// \\ingroup std.core\n";
	o << "public class " << suffix << " {\n";
	o << "\toperator new;\n";
	o << "\toperator new(cref x: f32)\n";
	o << "\t{\n";
	o << "\t\tpod = x.pod;\n";
	o << "\t}\n";
	if (bits > 32)
	{
		o << "\toperator new(cref x: f64) {\n";
		o << "\t\tpod = x.pod;\n";
		o << "\t}\n";
	}
	o << "\n";
	o << "\t#[nosuggest] operator new(self pod: __f32);\n";
	if (bits > 32)
		o << "\t#[nosuggest] operator new(self pod: __f64);\n";
	o << '\n';
	o << '\n';
	o << "\toperator ++self: ref " << suffix << " {\n";
	o << "\t\tpod = !!finc(pod);\n";
	o << "\t\treturn self;\n";
	o << "\t}\n";
	o << '\n';
	o << "\toperator self++: ref " << suffix << " {\n";
	o << "\t\tref tmp = new " << suffix << "(pod);\n";
	o << "\t\tpod = !!finc(pod);\n";
	o << "\t\treturn tmp;\n";
	o << "\t}\n";
	o << '\n';
	o << "\toperator --self: ref " << suffix << " {\n";
	o << "\t\tpod = !!fdec(pod);\n";
	o << "\t\treturn self;\n";
	o << "\t}\n";
	o << '\n';
	o << "\toperator self--: ref " << suffix << " {\n";
	o << "\t\tref tmp = new " << suffix << "(pod);\n";
	o << "\t\tpod = !!fdec(pod);\n";
	o << "\t\treturn tmp;\n";
	o << "\t}\n";
	o << '\n';

	auto craftOperator = [&](auto callback) {
		callback(bits, c);
		o << '\n';
	};

	auto craftMemberOperator = [&](const AnyString& op, const AnyString& intrinsic)
	{
		craftOperator([&](uint32_t b, char targetsign)
		{
			o << "\toperator " << op << " (cref x: " << targetsign << b << "): ref " << suffix << " {\n";
			o << "\t\tpod = !!" << intrinsic << "(pod, x.pod);\n";
			o << "\t\treturn self;\n";
			o << "\t}\n\n";

			o << "\t#[nosuggest] operator " << op << " (x: __" << targetsign << b << "): ref " << suffix << " {\n";
			o << "\t\tpod = !!" << intrinsic << "(pod, x);\n";
			o << "\t\treturn self;\n";
			o << "\t}\n";
		});
	};

	craftMemberOperator("+=", "fadd");
	craftMemberOperator("-=", "fsub");
	craftMemberOperator("*=", "fmul");
	craftMemberOperator("/=", "fdiv");

	o.trimRight();
	o << "\n\n";
	o << "private:\n";
	o << "\tvar pod = 0__" << suffix << ";\n";
	o << '\n';
	o << "} // class " << suffix << '\n';
	o << '\n';
	o << '\n';
	o << '\n';
	o << '\n';

	auto craft = [&](const AnyString& op, const AnyString& builtin, auto callback)
	{
		char sign = 'f';
		auto b = bits;
		callback(op, builtin, sign, b, "cref ", "cref ");
		callback(op, builtin, sign, b, "cref ", "__");
		callback(op, builtin, sign, b, "__", "cref ");
		callback(op, builtin, sign, b, "__", "__");
		o << '\n';
	};


	auto genGlobalCompareOperator = [&](AnyString op, AnyString builtin, char sign, uint32_t b, AnyString prefixA, AnyString prefixB)
	{
		o << "#[__nanyc_builtinalias: " << builtin;
		if (prefixA.first() == '_' or prefixB.first() == '_')
			o << ", nosuggest";
		o << "] public operator ";
		o << op << " (a: " << prefixA << suffix << ", b: " << prefixB << sign << b << "): ";
		o << ((prefixA.first() == '_' and prefixB.first() == '_') ? "__" : "ref ");
		o << "bool;\n";
	};
	craft(">",  "fgt",  genGlobalCompareOperator);
	craft(">=", "fgte", genGlobalCompareOperator);
	craft("<",  "flt",  genGlobalCompareOperator);
	craft("<=", "flte", genGlobalCompareOperator);
	craft("==",  "eq",  genGlobalCompareOperator);
	craft("!=",  "neq", genGlobalCompareOperator);

	o << '\n';
	o << '\n';

	auto genGlobalOperator = [&](AnyString op, AnyString builtin, char sign, uint32_t b, AnyString prefixA, AnyString prefixB)
	{
		bool atLeastOneBuiltin = (prefixA.first() != '_' or prefixB.first() != '_');
		o << "#[__nanyc_builtinalias: " << builtin;
		if (prefixA.first() == '_' or prefixB.first() == '_')
			o << ", nosuggest";
		o << "] public operator ";
		o << op << " (a: " << prefixA << suffix << ", b: " << prefixB << sign << b << "): ";
		o << ((atLeastOneBuiltin) ? "ref " : "__");
		o << suffix << ";\n";

		if (not atLeastOneBuiltin)
			o << '\n';
	};
	craft("+", "fadd", genGlobalOperator);
	craft("-", "fsub", genGlobalOperator);
	craft("/", "fdiv", genGlobalOperator);
	craft("*", "fmul", genGlobalOperator);

	o.trimRight();
	IO::File::SetContent(filename, o);
}


void craftClassInt(Clob& o, uint32_t bits, bool issigned, const AnyString& license, const AnyString& filename)
{
	o.clear();
	char c = (issigned) ? 'i' : 'u';
	ShortString16 suffix;
	suffix << c << bits;

	auto craftOperator = [&](auto callback) {
		callback(bits, c);
		if (issigned)
			callback(bits / 2, 'u');
		o << '\n';
	};

	auto craft = [&](const AnyString& op, const AnyString& builtin, auto callback)
	{
		char sign = issigned ? 'i' : 'u';
		for (uint32_t b = 64 /*bits*/; b >= 8; b /= 2)
		{
			callback(op, builtin, sign, b, "cref ", "cref ");
			callback(op, builtin, sign, b, "cref ", "__");
			callback(op, builtin, sign, b, "__", "cref ");
			callback(op, builtin, sign, b, "__", "__");
		}

		o << '\n';
	};


	o << license;
	o << "/// \\file    " << suffix << ".ny\n";
	o << "/// \\brief   Implementation of the class " << suffix << ", ";
	o << (issigned ? "Signed" : "Unsigned") << " integer with width of exactly " << bits << " bits\n";
	o << "/// \\ingroup std.core\n";
	o << "/// \\important THIS FILE IS AUTOMATICALLY GENERATED (see `nsl-core-generator.cpp`)\n";
	o << '\n';
	o << '\n';
	o << '\n';
	o << '\n';
	o << "/// \\brief   " << (issigned ? "Signed" : "Unsigned");
	o << " integer with width of exactly " << bits << " bits\n";
	o << "/// \\ingroup std.core\n";
	o << "public class " << suffix << " {\n";
	o << "\t//! Default constructor\n";
	o << "\toperator new;\n\n";
	craftOperator([&](uint32_t b, char targetsign)
	{
		for ( ; b >= 8; b /= 2)
		{
			o << "\toperator new (cref x: " << targetsign << b << ") {\n";
			o << "\t\tpod = x.pod;\n";
			o << "\t}\n";
		}
	});
	craftOperator([&](uint32_t b, char targetsign)
	{
		for ( ; b >= 8; b /= 2)
			o << "\t#[nosuggest] operator new (self pod: __" << targetsign << b << ");\n";
	});

	o << '\n';
	o << '\n';
	o << "\toperator ++self: ref " << suffix << " {\n";
	o << "\t\tpod = !!inc(pod);\n";
	o << "\t\treturn self;\n";
	o << "\t}\n";
	o << '\n';
	o << "\toperator self++: ref " << suffix << " {\n";
	o << "\t\tref tmp = new " << suffix << "(pod);\n";
	o << "\t\tpod = !!inc(pod);\n";
	o << "\t\treturn tmp;\n";
	o << "\t}\n";
	o << '\n';
	o << "\toperator --self: ref " << suffix << " {\n";
	o << "\t\tpod = !!dec(pod);\n";
	o << "\t\treturn self;\n";
	o << "\t}\n";
	o << '\n';
	o << "\toperator self--: ref " << suffix << " {\n";
	o << "\t\tref tmp = new " << suffix << "(pod);\n";
	o << "\t\tpod = !!dec(pod);\n";
	o << "\t\treturn tmp;\n";
	o << "\t}\n";
	o << '\n';
	o << '\n';

	auto craftMemberOperator = [&](const AnyString& op, const AnyString& intrinsic, bool prefix)
	{
		AnyString pr = (prefix and issigned) ? "i" : "";
		craftOperator([&](uint32_t b, char targetsign)
		{
			for ( ; b >= 8; b /= 2)
			{
				o << "\toperator " << op << " (cref x: " << targetsign << b << "): ref " << suffix << " {\n";
				o << "\t\tpod = !!" << pr << intrinsic << "(pod, x.pod);\n";
				o << "\t\treturn self;\n";
				o << "\t}\n\n";

				o << "\t#[nosuggest] operator " << op << " (x: __" << targetsign << b << "): ref " << suffix << " {\n";
				o << "\t\tpod = !!" << pr << intrinsic << "(pod, x);\n";
				o << "\t\treturn self;\n";
				o << "\t}\n\n";
			}
		});
		o << '\n';
	};

	craftMemberOperator("+=", "add", false);
	craftMemberOperator("-=", "sub", false);
	craftMemberOperator("*=", "mul", true);
	craftMemberOperator("/=", "div", true);

	o << "private:\n";
	o << "\tvar pod = 0__" << suffix << ";\n";
	o << '\n';
	o << "} // class " << suffix << '\n';
	o << '\n';
	o << '\n';
	o << '\n';
	o << '\n';


	bool canBeSigned = true;
	auto genGlobalCompareOperator = [&](AnyString op, AnyString builtin, char sign, uint32_t b, AnyString prefixA, AnyString prefixB)
	{
		o << "#[__nanyc_builtinalias: " << (canBeSigned and issigned ? "i" : "") << builtin;
		if (prefixA.first() == '_' or prefixB.first() == '_')
			o << ", nosuggest";
		o << "] public operator ";
		o << op << " (a: " << prefixA << suffix << ", b: " << prefixB << sign << b << "): ";
		o << ((prefixA.first() == '_' and prefixB.first() == '_') ? "__" : "ref ");
		o << "bool;\n";
	};
	craft(">",  "gt",  genGlobalCompareOperator);
	craft(">=", "gte", genGlobalCompareOperator);
	craft("<",  "lt",  genGlobalCompareOperator);
	craft("<=", "lte", genGlobalCompareOperator);

	o << '\n';
	o << '\n';
	o << '\n';
	o << '\n';

	canBeSigned = false;
	craft("==",  "eq",  genGlobalCompareOperator);
	craft("!=",  "neq", genGlobalCompareOperator);

	o << '\n';
	o << '\n';
	o << '\n';
	o << '\n';

	auto genGlobalOperator = [&](AnyString op, AnyString builtin, char sign, uint32_t b, AnyString prefixA, AnyString prefixB)
	{
		bool atLeastOneBuiltin = (prefixA.first() != '_' or prefixB.first() != '_');
		o << "#[__nanyc_builtinalias: " << builtin;
		if (prefixA.first() == '_' or prefixB.first() == '_')
			o << ", nosuggest";
		o << "] public operator ";
		o << op << " (a: " << prefixA << suffix << ", b: " << prefixB << sign << b << "): ";
		o << "any;\n";
		//o << ((atLeastOneBuiltin) ? "ref " : "__");
		//o << suffix << ";\n";

		if (not atLeastOneBuiltin)
			o << '\n';
	};
	craft("+", "add", genGlobalOperator);
	craft("-", "sub", genGlobalOperator);
	if (issigned)
	{
		craft("/", "idiv", genGlobalOperator);
		craft("*", "imul", genGlobalOperator);
	}
	else
	{
		craft("/", "div", genGlobalOperator);
		craft("*", "mul", genGlobalOperator);
	}

	o << '\n';
	o << '\n';
	o << '\n';
	o << '\n';

	craft("and", "and", genGlobalOperator);
	craft("or", "or", genGlobalOperator);
	craft("xor", "xor", genGlobalOperator);

	IO::File::SetContent(filename, o);
}


} // namespace




int main(int argc, char** argv)
{
	if (argc != 3)
	{
		std::cerr << "usage: <license-header-file> <folder>\n";
		return 1;
	}
	String license;
	if (IO::errNone != IO::File::LoadFromFile(license, argv[1]))
	{
		std::cerr << "failed to load header file from '" << argv[1] << "'\n";
		return 1;
	}

	AnyString folder = argv[2];
	Clob out;
	out.reserve(1024 * 32);
	String filename;
	filename.reserve(1024);

	for (uint32_t bits = 64; bits >= 8; bits /= 2)
	{
		filename.clear() << folder << "/u" << bits << ".ny";
		craftClassInt(out, bits, false, license, filename);
		filename.clear() << folder << "/i" << bits << ".ny";
		craftClassInt(out, bits, true,  license, filename);
	}

	for (uint32_t bits = 64; bits >= 32; bits /= 2)
	{
		filename.clear() << folder << "/f" << bits << ".ny";
		craftClassFloat(out, bits, license, filename);
	}

	return 0;
}
