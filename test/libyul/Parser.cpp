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
 * @date 2017
 * Unit tests for parsing Yul.
 */

#include <test/Options.h>

#include <test/libsolidity/ErrorCheck.h>
#include <test/libyul/Common.h>

#include <libyul/AsmParser.h>
#include <libyul/AsmAnalysis.h>
#include <libyul/AsmAnalysisInfo.h>
#include <libyul/Dialect.h>
#include <liblangutil/Scanner.h>
#include <liblangutil/ErrorReporter.h>

#include <boost/optional.hpp>
#include <boost/algorithm/string/replace.hpp>

#include <string>
#include <memory>

using namespace std;
using namespace dev;
using namespace langutil;

namespace yul
{
namespace test
{

namespace
{

bool parse(string const& _source, std::shared_ptr<Dialect> _dialect, ErrorReporter& errorReporter)
{
	try
	{
		auto scanner = make_shared<Scanner>(CharStream(_source, ""));
		auto parserResult = yul::Parser(errorReporter, _dialect).parse(scanner, false);
		if (parserResult)
		{
			yul::AsmAnalysisInfo analysisInfo;
			return (yul::AsmAnalyzer(
				analysisInfo,
				errorReporter,
				dev::test::Options::get().evmVersion(),
				boost::none,
				_dialect
			)).analyze(*parserResult);
		}
	}
	catch (FatalError const&)
	{
		BOOST_FAIL("Fatal error leaked.");
	}
	return false;
}

boost::optional<Error> parseAndReturnFirstError(string const& _source, shared_ptr<Dialect> _dialect, bool _allowWarnings = true)
{
	ErrorList errors;
	ErrorReporter errorReporter(errors);
	if (!parse(_source, _dialect, errorReporter))
	{
		BOOST_REQUIRE(!errors.empty());
		BOOST_CHECK_EQUAL(errors.size(), 1);
		return *errors.front();
	}
	else
	{
		// If success is true, there might still be an error in the assembly stage.
		if (_allowWarnings && Error::containsOnlyWarnings(errors))
			return {};
		else if (!errors.empty())
		{
			if (!_allowWarnings)
				BOOST_CHECK_EQUAL(errors.size(), 1);
			return *errors.front();
		}
	}
	return {};
}

bool successParse(std::string const& _source, shared_ptr<Dialect> _dialect = Dialect::yul(), bool _allowWarnings = true)
{
	return !parseAndReturnFirstError(_source, _dialect, _allowWarnings);
}

Error expectError(std::string const& _source, shared_ptr<Dialect> _dialect = Dialect::yul(), bool _allowWarnings = false)
{

	auto error = parseAndReturnFirstError(_source, _dialect, _allowWarnings);
	BOOST_REQUIRE(error);
	return *error;
}

}

#define CHECK_ERROR_DIALECT(text, typ, substring, dialect) \
do \
{ \
	Error err = expectError((text), dialect, false); \
	BOOST_CHECK(err.type() == (Error::Type::typ)); \
	BOOST_CHECK(dev::solidity::searchErrorMessage(err, (substring))); \
} while(0)

#define CHECK_ERROR(text, typ, substring) CHECK_ERROR_DIALECT(text, typ, substring, Dialect::yul())

BOOST_AUTO_TEST_SUITE(YulParser)

BOOST_AUTO_TEST_CASE(smoke_test)
{
	BOOST_CHECK(successParse("{ }"));
}

BOOST_AUTO_TEST_CASE(vardecl)
{
	BOOST_CHECK(successParse("{ let x:u256 := 7:u256 }"));
}

BOOST_AUTO_TEST_CASE(vardecl_bool)
{
	BOOST_CHECK(successParse("{ let x:bool := true:bool }"));
	BOOST_CHECK(successParse("{ let x:bool := false:bool }"));
}

BOOST_AUTO_TEST_CASE(vardecl_empty)
{
	BOOST_CHECK(successParse("{ let x:u256 }"));
}

BOOST_AUTO_TEST_CASE(assignment)
{
	BOOST_CHECK(successParse("{ let x:u256 := 2:u256 let y:u256 := x }"));
}

BOOST_AUTO_TEST_CASE(vardecl_complex)
{
	BOOST_CHECK(successParse("{ function add(a:u256, b:u256) -> c:u256 {} let y:u256 := 2:u256 let x:u256 := add(7:u256, add(6:u256, y)) }"));
}

BOOST_AUTO_TEST_CASE(blocks)
{
	BOOST_CHECK(successParse("{ let x:u256 := 7:u256 { let y:u256 := 3:u256 } { let z:u256 := 2:u256 } }"));
}

BOOST_AUTO_TEST_CASE(function_definitions)
{
	BOOST_CHECK(successParse("{ function f() { } function g(a:u256) -> x:u256 { } }"));
}

BOOST_AUTO_TEST_CASE(function_definitions_multiple_args)
{
	BOOST_CHECK(successParse("{ function f(a:u256, d:u256) { } function g(a:u256, d:u256) -> x:u256, y:u256 { } }"));
}

BOOST_AUTO_TEST_CASE(function_calls)
{
	BOOST_CHECK(successParse("{ function f(a:u256) -> b:u256 {} function g(a:u256, b:u256, c:u256) {} function x() { g(1:u256, 2:u256, f(3:u256)) x() } }"));
}

BOOST_AUTO_TEST_CASE(tuple_assignment)
{
	BOOST_CHECK(successParse("{ function f() -> a:u256, b:u256, c:u256 {} let x:u256, y:u256, z:u256 := f() }"));
}

BOOST_AUTO_TEST_CASE(label)
{
	CHECK_ERROR("{ label: }", ParserError, "Labels are not supported.");
}

BOOST_AUTO_TEST_CASE(instructions)
{
	CHECK_ERROR("{ pop }", ParserError, "Call or assignment expected.");
}

BOOST_AUTO_TEST_CASE(push)
{
	CHECK_ERROR("{ 0x42:u256 }", ParserError, "Call or assignment expected.");
}

BOOST_AUTO_TEST_CASE(assign_from_stack)
{
	CHECK_ERROR("{ =: x:u256 }", ParserError, "Literal or identifier expected.");
}

BOOST_AUTO_TEST_CASE(empty_call)
{
	CHECK_ERROR("{ () }", ParserError, "Literal or identifier expected.");
}

BOOST_AUTO_TEST_CASE(tokens_as_identifers)
{
	BOOST_CHECK(successParse("{ let return:u256 := 1:u256 }"));
	BOOST_CHECK(successParse("{ let byte:u256 := 1:u256 }"));
	BOOST_CHECK(successParse("{ let address:u256 := 1:u256 }"));
	BOOST_CHECK(successParse("{ let bool:u256 := 1:u256 }"));
}

BOOST_AUTO_TEST_CASE(lacking_types)
{
	CHECK_ERROR("{ let x := 1:u256 }", ParserError, "Expected identifier but got '='");
	CHECK_ERROR("{ let x:u256 := 1 }", ParserError, "Expected ':' but got '}'");
	CHECK_ERROR("{ function f(a) {} }", ParserError, "Expected ':' but got ')'");
	CHECK_ERROR("{ function f(a:u256) -> b {} }", ParserError, "Expected ':' but got '{'");
}

BOOST_AUTO_TEST_CASE(invalid_types)
{
	/// testing invalid literal
	/// NOTE: these will need to change when types are compared
	CHECK_ERROR("{ let x:bool := 1:invalid }", TypeError, "\"invalid\" is not a valid type (user defined types are not yet supported).");
	/// testing invalid variable declaration
	CHECK_ERROR("{ let x:invalid := 1:bool }", TypeError, "\"invalid\" is not a valid type (user defined types are not yet supported).");
	CHECK_ERROR("{ function f(a:invalid) {} }", TypeError, "\"invalid\" is not a valid type (user defined types are not yet supported).");
}

BOOST_AUTO_TEST_CASE(number_literals)
{
	BOOST_CHECK(successParse("{ let x:u256 := 1:u256 }"));
	CHECK_ERROR("{ let x:u256 := .1:u256 }", ParserError, "Invalid number literal.");
	CHECK_ERROR("{ let x:u256 := 1e5:u256 }", ParserError, "Invalid number literal.");
	CHECK_ERROR("{ let x:u256 := 67.235:u256 }", ParserError, "Invalid number literal.");
	CHECK_ERROR("{ let x:u256 := 0x1ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff:u256 }", TypeError, "Number literal too large (> 256 bits)");
}

BOOST_AUTO_TEST_CASE(builtin_types)
{
	BOOST_CHECK(successParse("{ let x:bool := true:bool }"));
	BOOST_CHECK(successParse("{ let x:u8 := 1:u8 }"));
	BOOST_CHECK(successParse("{ let x:s8 := 1:u8 }"));
	BOOST_CHECK(successParse("{ let x:u32 := 1:u32 }"));
	BOOST_CHECK(successParse("{ let x:s32 := 1:s32 }"));
	BOOST_CHECK(successParse("{ let x:u64 := 1:u64 }"));
	BOOST_CHECK(successParse("{ let x:s64 := 1:s64 }"));
	BOOST_CHECK(successParse("{ let x:u128 := 1:u128 }"));
	BOOST_CHECK(successParse("{ let x:s128 := 1:s128 }"));
	BOOST_CHECK(successParse("{ let x:u256 := 1:u256 }"));
	BOOST_CHECK(successParse("{ let x:s256 := 1:s256 }"));
}

BOOST_AUTO_TEST_CASE(recursion_depth)
{
	string input;
	for (size_t i = 0; i < 20000; i++)
		input += "{";
	input += "let x:u256 := 0:u256";
	for (size_t i = 0; i < 20000; i++)
		input += "}";

	CHECK_ERROR(input, ParserError, "recursion");
}

BOOST_AUTO_TEST_CASE(multiple_assignment)
{
	CHECK_ERROR("{ let x:u256 function f() -> a:u256, b:u256 {} 123:u256, x := f() }", ParserError, "Label name / variable name must precede \",\" (multiple assignment).");
	CHECK_ERROR("{ let x:u256 function f() -> a:u256, b:u256 {} x, 123:u256 := f() }", ParserError, "Variable name expected in multiple assignment.");

	/// NOTE: Travis hiccups if not having a variable
	char const* text = R"(
	{
		function f(a:u256) -> r1:u256, r2:u256 {
			r1 := a
			r2 := 7:u256
		}
		let x:u256 := 9:u256
		let y:u256 := 2:u256
		x, y := f(x)
	}
	)";
	BOOST_CHECK(successParse(text));
}

BOOST_AUTO_TEST_CASE(if_statement)
{
	BOOST_CHECK(successParse("{ if true:bool {} }"));
	BOOST_CHECK(successParse("{ if false:bool { let x:u256 := 3:u256 } }"));
	BOOST_CHECK(successParse("{ function f() -> x:bool {} if f() { let b:bool := f() } }"));
}

BOOST_AUTO_TEST_CASE(if_statement_invalid)
{
	CHECK_ERROR("{ if let x:u256 {} }", ParserError, "Literal or identifier expected.");
	CHECK_ERROR("{ if true:bool let x:u256 := 3:u256 }", ParserError, "Expected '{' but got reserved keyword 'let'");
	// TODO change this to an error once we check types.
	BOOST_CHECK(successParse("{ if 42:u256 { } }"));
}

BOOST_AUTO_TEST_CASE(builtins_parser)
{
	struct SimpleDialect: public Dialect
	{
		SimpleDialect(): Dialect(AsmFlavour::Strict) {}
		BuiltinFunction const* builtin(YulString _name) const override
		{
			return _name == "builtin"_yulstring ? &f : nullptr;
		}
		BuiltinFunction f;
	};

	shared_ptr<Dialect> dialect = make_shared<SimpleDialect>();
	CHECK_ERROR_DIALECT("{ let builtin := 6 }", ParserError, "Cannot use builtin function name \"builtin\" as identifier name.", dialect);
	CHECK_ERROR_DIALECT("{ function builtin() {} }", ParserError, "Cannot use builtin function name \"builtin\" as identifier name.", dialect);
	CHECK_ERROR_DIALECT("{ builtin := 6 }", ParserError, "Cannot assign to builtin function \"builtin\".", dialect);
}

BOOST_AUTO_TEST_CASE(builtins_analysis)
{
	struct SimpleDialect: public Dialect
	{
		SimpleDialect(): Dialect(AsmFlavour::Strict) {}
		BuiltinFunction const* builtin(YulString _name) const override
		{
			return _name == "builtin"_yulstring ? &f : nullptr;
		}
		BuiltinFunction f{"builtin"_yulstring, vector<Type>(2), vector<Type>(3), false, false};
	};

	shared_ptr<Dialect> dialect = make_shared<SimpleDialect>();
	BOOST_CHECK(successParse("{ let a, b, c := builtin(1, 2) }", dialect));
	CHECK_ERROR_DIALECT("{ let a, b, c := builtin(1) }", TypeError, "Function expects 2 arguments but got 1", dialect);
	CHECK_ERROR_DIALECT("{ let a, b := builtin(1, 2) }", DeclarationError, "Variable count mismatch: 2 variables and 3 values.", dialect);
}

BOOST_AUTO_TEST_SUITE_END()

}
} // end namespaces
