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
 * @date 2018
 * Templatized list of simplification rules.
 */

#pragma once

#include <vector>
#include <functional>

#include <libevmasm/Instruction.h>
#include <libevmasm/SimplificationRule.h>

#include <libdevcore/CommonData.h>

namespace dev
{
namespace solidity
{

template <class S> S divWorkaround(S const& _a, S const& _b)
{
	return (S)(bigint(_a) / bigint(_b));
}

template <class S> S modWorkaround(S const& _a, S const& _b)
{
	return (S)(bigint(_a) % bigint(_b));
}

// This part of simplificationRuleList below was split out to prevent
// stack overflows in the JavaScript optimizer for emscripten builds
// that affected certain browser versions.
template <class Pattern>
std::vector<SimplificationRule<Pattern>> simplificationRuleListPart1(
	Pattern A,
	Pattern B,
	Pattern C,
	Pattern X,
	Pattern Y
)
{
	return std::vector<SimplificationRule<Pattern>> {
		// arithmetic on constants
		{{Instruction::ADD, {A, B}}, [=]{ return A.d() + B.d(); }, false},
		{{Instruction::SADD, {A, B}}, [=]{ return A.d() + B.d(); }, false},
		{{Instruction::MUL, {A, B}}, [=]{ return A.d() * B.d(); }, false},
		{{Instruction::SMUL, {A, B}}, [=]{ return A.d() * B.d(); }, false},
		{{Instruction::SUB, {A, B}}, [=]{ return A.d() - B.d(); }, false},
		{{Instruction::SSUB, {A, B}}, [=]{ return A.d() - B.d(); }, false},
		{{Instruction::DIV, {A, B}}, [=]{ return B.d() == 0 ? 0 : divWorkaround(A.d(), B.d()); }, false},
		{{Instruction::SDIV, {A, B}}, [=]{ return B.d() == 0 ? 0 : s2u(divWorkaround(u2s(A.d()), u2s(B.d()))); }, false},
		{{Instruction::MOD, {A, B}}, [=]{ return B.d() == 0 ? 0 : modWorkaround(A.d(), B.d()); }, false},
		{{Instruction::SMOD, {A, B}}, [=]{ return B.d() == 0 ? 0 : s2u(modWorkaround(u2s(A.d()), u2s(B.d()))); }, false},
		{{Instruction::EXP, {A, B}}, [=]{ return u256(boost::multiprecision::powm(bigint(A.d()), bigint(B.d()), bigint(1) << 256)); }, false},
		{{Instruction::NOT, {A}}, [=]{ return ~A.d(); }, false},
		{{Instruction::LT, {A, B}}, [=]() -> u256 { return A.d() < B.d() ? 1 : 0; }, false},
		{{Instruction::GT, {A, B}}, [=]() -> u256 { return A.d() > B.d() ? 1 : 0; }, false},
		{{Instruction::SLT, {A, B}}, [=]() -> u256 { return u2s(A.d()) < u2s(B.d()) ? 1 : 0; }, false},
		{{Instruction::SGT, {A, B}}, [=]() -> u256 { return u2s(A.d()) > u2s(B.d()) ? 1 : 0; }, false},
		{{Instruction::EQ, {A, B}}, [=]() -> u256 { return A.d() == B.d() ? 1 : 0; }, false},
		{{Instruction::ISZERO, {A}}, [=]() -> u256 { return A.d() == 0 ? 1 : 0; }, false},
		{{Instruction::AND, {A, B}}, [=]{ return A.d() & B.d(); }, false},
		{{Instruction::OR, {A, B}}, [=]{ return A.d() | B.d(); }, false},
		{{Instruction::XOR, {A, B}}, [=]{ return A.d() ^ B.d(); }, false},
		{{Instruction::BYTE, {A, B}}, [=]{ return A.d() >= 32 ? 0 : (B.d() >> unsigned(8 * (31 - A.d()))) & 0xff; }, false},
		{{Instruction::ADDMOD, {A, B, C}}, [=]{ return C.d() == 0 ? 0 : u256((bigint(A.d()) + bigint(B.d())) % C.d()); }, false},
		{{Instruction::MULMOD, {A, B, C}}, [=]{ return C.d() == 0 ? 0 : u256((bigint(A.d()) * bigint(B.d())) % C.d()); }, false},
		{{Instruction::MULMOD, {A, B, C}}, [=]{ return A.d() * B.d(); }, false},
		{{Instruction::SIGNEXTEND, {A, B}}, [=]() -> u256 {
			if (A.d() >= 31)
				return B.d();
			unsigned testBit = unsigned(A.d()) * 8 + 7;
			u256 mask = (u256(1) << testBit) - 1;
			return u256(boost::multiprecision::bit_test(B.d(), testBit) ? B.d() | ~mask : B.d() & mask);
		}, false},
		{{Instruction::SHL, {A, B}}, [=]{
			if (A.d() > 255)
				return u256(0);
			return u256(bigint(B.d()) << unsigned(A.d()));
		}, false},
		{{Instruction::SHR, {A, B}}, [=]{
			if (A.d() > 255)
				return u256(0);
			return B.d() >> unsigned(A.d());
		}, false},

		// invariants involving known constants
		{{Instruction::ADD, {X, 0}}, [=]{ return X; }, false},
		{{Instruction::ADD, {0, X}}, [=]{ return X; }, false},
		{{Instruction::SADD, {X, 0}}, [=]{ return X; }, false},
		{{Instruction::SADD, {0, X}}, [=]{ return X; }, false},
		{{Instruction::SUB, {X, 0}}, [=]{ return X; }, false},
		{{Instruction::SSUB, {X, 0}}, [=]{ return X; }, false},
		{{Instruction::MUL, {X, 0}}, [=]{ return u256(0); }, true},
		{{Instruction::MUL, {0, X}}, [=]{ return u256(0); }, true},
		{{Instruction::MUL, {X, 1}}, [=]{ return X; }, false},
		{{Instruction::MUL, {1, X}}, [=]{ return X; }, false},
		{{Instruction::MUL, {X, u256(-1)}}, [=]() -> Pattern { return {Instruction::SUB, {0, X}}; }, false},
		{{Instruction::MUL, {u256(-1), X}}, [=]() -> Pattern { return {Instruction::SUB, {0, X}}; }, false},
		{{Instruction::SMUL, {X, 0}}, [=]{ return u256(0); }, true},
		{{Instruction::SMUL, {0, X}}, [=]{ return u256(0); }, true},
		{{Instruction::SMUL, {X, 1}}, [=]{ return X; }, false},
		{{Instruction::SMUL, {1, X}}, [=]{ return X; }, false},
		{{Instruction::SMUL, {X, u256(-1)}}, [=]() -> Pattern { return {Instruction::SSUB, {0, X}}; }, false},
		{{Instruction::SMUL, {u256(-1), X}}, [=]() -> Pattern { return {Instruction::SSUB, {0, X}}; }, false},
		{{Instruction::DIV, {X, 0}}, [=]{ return u256(0); }, true},
		{{Instruction::DIV, {0, X}}, [=]{ return u256(0); }, true},
		{{Instruction::DIV, {X, 1}}, [=]{ return X; }, false},
		{{Instruction::SDIV, {X, 0}}, [=]{ return u256(0); }, true},
		{{Instruction::SDIV, {0, X}}, [=]{ return u256(0); }, true},
		{{Instruction::SDIV, {X, 1}}, [=]{ return X; }, false},
		{{Instruction::AND, {X, ~u256(0)}}, [=]{ return X; }, false},
		{{Instruction::AND, {~u256(0), X}}, [=]{ return X; }, false},
		{{Instruction::AND, {X, 0}}, [=]{ return u256(0); }, true},
		{{Instruction::AND, {0, X}}, [=]{ return u256(0); }, true},
		{{Instruction::OR, {X, 0}}, [=]{ return X; }, false},
		{{Instruction::OR, {0, X}}, [=]{ return X; }, false},
		{{Instruction::OR, {X, ~u256(0)}}, [=]{ return ~u256(0); }, true},
		{{Instruction::OR, {~u256(0), X}}, [=]{ return ~u256(0); }, true},
		{{Instruction::XOR, {X, 0}}, [=]{ return X; }, false},
		{{Instruction::XOR, {0, X}}, [=]{ return X; }, false},
		{{Instruction::MOD, {X, 0}}, [=]{ return u256(0); }, true},
		{{Instruction::MOD, {0, X}}, [=]{ return u256(0); }, true},
		{{Instruction::EQ, {X, 0}}, [=]() -> Pattern { return {Instruction::ISZERO, {X}}; }, false },
		{{Instruction::EQ, {0, X}}, [=]() -> Pattern { return {Instruction::ISZERO, {X}}; }, false },

		// operations involving an expression and itself
		{{Instruction::AND, {X, X}}, [=]{ return X; }, true},
		{{Instruction::OR, {X, X}}, [=]{ return X; }, true},
		{{Instruction::XOR, {X, X}}, [=]{ return u256(0); }, true},
		{{Instruction::SUB, {X, X}}, [=]{ return u256(0); }, true},
		{{Instruction::SSUB, {X, X}}, [=]{ return u256(0); }, true},
		{{Instruction::EQ, {X, X}}, [=]{ return u256(1); }, true},
		{{Instruction::LT, {X, X}}, [=]{ return u256(0); }, true},
		{{Instruction::SLT, {X, X}}, [=]{ return u256(0); }, true},
		{{Instruction::GT, {X, X}}, [=]{ return u256(0); }, true},
		{{Instruction::SGT, {X, X}}, [=]{ return u256(0); }, true},
		{{Instruction::MOD, {X, X}}, [=]{ return u256(0); }, true},

		// logical instruction combinations
		{{Instruction::NOT, {{Instruction::NOT, {X}}}}, [=]{ return X; }, false},
		{{Instruction::XOR, {X, {Instruction::XOR, {X, Y}}}}, [=]{ return Y; }, true},
		{{Instruction::XOR, {X, {Instruction::XOR, {Y, X}}}}, [=]{ return Y; }, true},
		{{Instruction::XOR, {{Instruction::XOR, {X, Y}}, X}}, [=]{ return Y; }, true},
		{{Instruction::XOR, {{Instruction::XOR, {Y, X}}, X}}, [=]{ return Y; }, true},
		{{Instruction::OR, {X, {Instruction::AND, {X, Y}}}}, [=]{ return X; }, true},
		{{Instruction::OR, {X, {Instruction::AND, {Y, X}}}}, [=]{ return X; }, true},
		{{Instruction::OR, {{Instruction::AND, {X, Y}}, X}}, [=]{ return X; }, true},
		{{Instruction::OR, {{Instruction::AND, {Y, X}}, X}}, [=]{ return X; }, true},
		{{Instruction::AND, {X, {Instruction::OR, {X, Y}}}}, [=]{ return X; }, true},
		{{Instruction::AND, {X, {Instruction::OR, {Y, X}}}}, [=]{ return X; }, true},
		{{Instruction::AND, {{Instruction::OR, {X, Y}}, X}}, [=]{ return X; }, true},
		{{Instruction::AND, {{Instruction::OR, {Y, X}}, X}}, [=]{ return X; }, true},
		{{Instruction::AND, {X, {Instruction::NOT, {X}}}}, [=]{ return u256(0); }, true},
		{{Instruction::AND, {{Instruction::NOT, {X}}, X}}, [=]{ return u256(0); }, true},
		{{Instruction::OR, {X, {Instruction::NOT, {X}}}}, [=]{ return ~u256(0); }, true},
		{{Instruction::OR, {{Instruction::NOT, {X}}, X}}, [=]{ return ~u256(0); }, true},
	};
}


// This part of simplificationRuleList below was split out to prevent
// stack overflows in the JavaScript optimizer for emscripten builds
// that affected certain browser versions.
template <class Pattern>
std::vector<SimplificationRule<Pattern>> simplificationRuleListPart2(
	Pattern A,
	Pattern B,
	Pattern,
	Pattern X,
	Pattern Y
)
{
	std::vector<SimplificationRule<Pattern>> rules;

	// Replace MOD X, <power-of-two> with AND X, <power-of-two> - 1
	for (size_t i = 0; i < 256; ++i)
	{
		u256 value = u256(1) << i;
		rules.push_back({
			{Instruction::MOD, {X, value}},
			[=]() -> Pattern { return {Instruction::AND, {X, value - 1}}; },
			false
		});
	}

	for (auto const& op: std::vector<Instruction>{
		Instruction::ADDRESS,
		Instruction::CALLER,
		Instruction::ORIGIN,
		Instruction::COINBASE
	})
	{
		u256 const mask = (u256(1) << 160) - 1;
		rules.push_back({
			{Instruction::AND, {{op, mask}}},
			[=]() -> Pattern { return op; },
			false
		});
		rules.push_back({
			{Instruction::AND, {{mask, op}}},
			[=]() -> Pattern { return op; },
			false
		});
	}

	// Double negation of opcodes with boolean result
	for (auto const& op: std::vector<Instruction>{
		Instruction::EQ,
		Instruction::LT,
		Instruction::SLT,
		Instruction::GT,
		Instruction::SGT
	})
		rules.push_back({
			{Instruction::ISZERO, {{Instruction::ISZERO, {{op, {X, Y}}}}}},
			[=]() -> Pattern { return {op, {X, Y}}; },
			false
		});

	rules.push_back({
		{Instruction::ISZERO, {{Instruction::ISZERO, {{Instruction::ISZERO, {X}}}}}},
		[=]() -> Pattern { return {Instruction::ISZERO, {X}}; },
		false
	});

	rules.push_back({
		{Instruction::ISZERO, {{Instruction::XOR, {X, Y}}}},
		[=]() -> Pattern { return { Instruction::EQ, {X, Y} }; },
		false
	});

	// Associative operations
	for (auto const& opFun: std::vector<std::pair<Instruction,std::function<u256(u256 const&,u256 const&)>>>{
		{Instruction::ADD, std::plus<u256>()},
		{Instruction::SADD, std::plus<u256>()},
		{Instruction::MUL, std::multiplies<u256>()},
		{Instruction::SMUL, std::multiplies<u256>()},
		{Instruction::AND, std::bit_and<u256>()},
		{Instruction::OR, std::bit_or<u256>()},
		{Instruction::XOR, std::bit_xor<u256>()}
	})
	{
		auto op = opFun.first;
		auto fun = opFun.second;
		// Moving constants to the outside, order matters here - we first add rules
		// for constants and then for non-constants.
		// xa can be (X, A) or (A, X)
		for (auto xa: {std::vector<Pattern>{X, A}, std::vector<Pattern>{A, X}})
		{
			rules += std::vector<SimplificationRule<Pattern>>{{
				// (X+A)+B -> X+(A+B)
				{op, {{op, xa}, B}},
				[=]() -> Pattern { return {op, {X, fun(A.d(), B.d())}}; },
				false
			}, {
				// (X+A)+Y -> (X+Y)+A
				{op, {{op, xa}, Y}},
				[=]() -> Pattern { return {op, {{op, {X, Y}}, A}}; },
				false
			}, {
				// B+(X+A) -> X+(A+B)
				{op, {B, {op, xa}}},
				[=]() -> Pattern { return {op, {X, fun(A.d(), B.d())}}; },
				false
			}, {
				// Y+(X+A) -> (Y+X)+A
				{op, {Y, {op, xa}}},
				[=]() -> Pattern { return {op, {{op, {Y, X}}, A}}; },
				false
			}};
		}
	}
	for (auto const& add_sub: std::vector<std::pair<Instruction, Instruction>>{
		{Instruction::ADD, Instruction::SUB},
		{Instruction::SADD, Instruction::SSUB}
	})
	{
		auto const& add = add_sub.first;
		auto const& sub = add_sub.second;
		for (auto xa: {std::vector<Pattern>{X, A}, std::vector<Pattern>{A, X}})
		{
			rules += std::vector<SimplificationRule<Pattern>>{{
				// (X + A) - B -> X + (A - B), X - (B - A)
				{sub, {{add, xa}, B}},
				[=]() -> Pattern {
					if (A.d() < B.d()) {
						return {sub, {X, B.d() - A.d()}};
					} else {
						return {add, {X, A.d() - B.d()}};
					}
				},
				false
			}, {
				// B - (X + A) -> (B - A) - X
				{sub, {B, {add, xa}}},
				[=]() -> Pattern { return {sub, {B.d() - A.d(), X}}; },
				false
			}};
		}
		rules += std::vector<SimplificationRule<Pattern>>{{
			// (X - A) + B -> X + (B - A), X - (A - B)
			{add, {{sub, {X, A}}, B}},
			[=]() -> Pattern {
				if (B.d() < A.d()) {
					return {sub, {X, A.d() - B.d()}};
				} else {
					return {add, {X, B.d() - A.d()}};
				}
			},
			false
		}, {
			// B + (X - A) -> X + (B - A), X - (A - B)
			{add, {B, {sub, {X, A}}}},
			[=]() -> Pattern {
				if (B.d() < A.d()) {
					return {sub, {X, A.d() - B.d()}};
				} else {
					return {add, {X, B.d() - A.d()}};
				}
			},
			false
		}, {
			// (X - A) - B -> X - (A + B)
			// B - (X - A) -> (A + B) - X -> overflow?
			{sub, {{sub, {X, A}}, B}},
			[=]() -> Pattern { return {sub, {X, A.d() + B.d()}}; },
			false
		}, {
			// (A - X) - B -> (A - B) - X
			// B - (A - X) -> X + (B - A) -> underflow?
			{sub, {{sub, {A, X}}, B}},
			[=]() -> Pattern { return {sub, {A.d() - B.d(), X}}; },
			false
		}};

		// move constants across subtractions
		rules += std::vector<SimplificationRule<Pattern>>{{
			// (X + A) - Y -> (X - Y) + A
			{sub, {{add, {X, A}}, Y}},
			[=]() -> Pattern { return {add, {{sub, {X, Y}}, A}}; },
			false
		}, {
			// (A + X) - Y -> (X - Y) + A
			{sub, {{add, {A, X}}, Y}},
			[=]() -> Pattern { return {add, {{sub, {X, Y}}, A}}; },
			false
		}, {
			// X - (Y + A) -> (X - Y) - A
			{sub, {X, {add, {Y, A}}}},
			[=]() -> Pattern { return {sub, {{sub, {X, Y}}, A}}; },
			false
		}, {
			// X - (A + Y) -> (X - Y) - A
			{sub, {X, {add, {A, Y}}}},
			[=]() -> Pattern { return {sub, {{sub, {X, Y}}, A}}; },
			false
		}};
	}

	return rules;
}

/// @returns a list of simplification rules given certain match placeholders.
/// A, B and C should represent constants, X and Y arbitrary expressions.
/// The simplifications should never change the order of evaluation of
/// arbitrary operations.
template <class Pattern>
std::vector<SimplificationRule<Pattern>> simplificationRuleList(
	Pattern A,
	Pattern B,
	Pattern C,
	Pattern X,
	Pattern Y
)
{
	std::vector<SimplificationRule<Pattern>> rules;
	rules += simplificationRuleListPart1(A, B, C, X, Y);
	rules += simplificationRuleListPart2(A, B, C, X, Y);
	return rules;
}

}
}
