#include "stdafx.h"
#include "tableaux.h"

// ----------------------------------------------------------------------------
// BaseSignedFormula

Formula BaseSignedFormula::getFormula() const
{
	return _f;
}

bool BaseSignedFormula::getSign() const
{
	return _sign;
}

void BaseSignedFormula::printSignedFormula(ostream & ostr) const
{
	ostr << (getSign() ? "T " : "F ") << "(" << getFormula() << ")";
}

BaseSignedFormula::TableauxType BaseSignedFormula::getType() const
{
	// We treat atoms as special type of formulae
	if (_f->getType() == BaseFormula::T_ATOM)
	{
		return TT_ATOM;
	}

	// Alpha-type formulae are...
	if (
		// T ~X
		_sign && _f->getType() == BaseFormula::T_NOT ||
		// F ~X
		!_sign && _f->getType() == BaseFormula::T_NOT ||
		// T (X /\ Y)
		_sign && _f->getType() == BaseFormula::T_AND ||
		// F (X \/ Y)
		!_sign && _f->getType() == BaseFormula::T_OR ||
		// F (X => Y)
		!_sign && _f->getType() == BaseFormula::T_IMP
	)
	{
		return TT_ALPHA;
	}

	// Beta-type formulae are...
	if (
		// F (X /\ Y)
		!_sign && _f->getType() == BaseFormula::T_AND ||
		// T (X \/ Y)
		_sign && _f->getType() == BaseFormula::T_OR ||
		// T (X => Y)
		_sign && _f->getType() == BaseFormula::T_IMP
		)
	{
		return TT_BETA;
	}

	// Gamma-type formulae are...
	if (
		// T (Av)X(v)
		_sign && _f->getType() == BaseFormula::T_FORALL ||
		// F (Ev)X(v)
		!_sign && _f->getType() == BaseFormula::T_EXISTS
		)
	{
		return TT_GAMMA;
	}

	// Delta-type formulae are...
	if (
		// F (Av)X(v)
		!_sign && _f->getType() == BaseFormula::T_FORALL ||
		// T (Ev)X(v)
		_sign && _f->getType() == BaseFormula::T_EXISTS
		)
	{
		return TT_DELTA;
	}

	throw "Not applicable";
}

// END BaseSignedFormula
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
// Tableaux

Tableaux::Tableaux(const Formula & root)
{
	// The original formula should be transformed to match the correct input for tableaux
	Formula transformed;

	// First, eliminate all equivalents from the formula, and then eliminate all constants from the formula
	transformed = root->releaseIff()->absorbConstants();

	// If the transformed formula is a logic constant true, then...
	if (transformed->getType() == BaseFormula::T_TRUE)
	{
		// ... transform the formula into its equivalent form without logic constants
		transformed = ((True*)transformed.get())->transformToDisjunction();
	}
	// If the transformed formula is a logic constant false, then...
	else if (transformed->getType() == BaseFormula::T_FALSE)
	{
		// ... transform the formula into its equivalent form without logic constants
		transformed = ((False*)transformed.get())->transformToConjunction();
	}
	// Otherwise, do nothing

	_root = make_shared<BaseSignedFormula>(transformed, false);
	/* By here, the formula _root is equivalent to the beginning formula root,
	so if the formula _root is unsatisfiable, then the formula root is unsatisfiable */
	_result = prove();
}

string Tableaux::getResult() const
{
	return _result ? "TAUTOLOGY" : "NOT A TAUTOLOGY";
}

bool Tableaux::prove(deque<SignedFormula>&& d_formulae, deque<FunctionSymbol>&& d_constants, int tabs) const
{
	if (!d_formulae.empty())
	{
		// Writing the current state of tableaux to the standard output
		cout << string(tabs, '\t');
		cout << d_formulae << ", " << d_constants << endl;

		SignedFormula rule;
		BaseSignedFormula::TableauxType tType;

		if (checkIfExistsComplementaryPairOfLiterals(d_formulae))
		{
			// close the branch
			return true;
		}
		else if (checkIfExistsNonGammaRule(d_formulae, rule, tType))
		{
			if (tType == BaseSignedFormula::TT_ALPHA || tType == BaseSignedFormula::TT_BETA)
			{
				switch (rule->getFormula()->getType())
				{
					case BaseFormula::T_NOT:
						return notRules(move(d_formulae), move(d_constants), rule, tabs);
					case BaseFormula::T_AND:
						return andRules(move(d_formulae), move(d_constants), rule, tabs);
					case BaseFormula::T_OR:
						return orRules(move(d_formulae), move(d_constants), rule, tabs);
					case BaseFormula::T_IMP:
						return impRules(move(d_formulae), move(d_constants), rule, tabs);
					default:
						throw "Not applicable: Unknown formula type for signed formula type ALPHA/BETA";
				}
			}
			else if (tType == BaseSignedFormula::TT_DELTA)
			{
				switch (rule->getFormula()->getType())
				{
					case BaseFormula::T_FORALL:
					{
						if (rule->getSign() == false)
						{
							return forallRules(move(d_formulae), move(d_constants), rule, tabs);
						} 
						else
						{
							throw "Not applicable: Unknown sign for signed formula type DELTA";
						}
					}
					case BaseFormula::T_EXISTS:
					{
						if (rule->getSign() == true)
						{
							return existsRules(move(d_formulae), move(d_constants), rule, tabs);
						} 
						else
						{
							throw "Not applicable: Unknown sign for signed formula type DELTA";
						}
					}
					default:
						throw "Not applicable: Unknown formula type for signed formula type DELTA";
				}
			}
			else if (tType == BaseSignedFormula::TT_ATOM)
			{
				return atomRules(move(d_formulae), move(d_constants), rule, tabs);
			}

			throw "Not applicable: unknown type of signed formula";
		}
		else
		{
			bool isOpenedBranch = checkIfShouldBranchBeOpenForGammaRule(d_formulae, d_constants);
			if (isOpenedBranch)
			{
				// mark the branch as open 
				return false;
			}
			else 
			{
				return prove(move(d_formulae), move(d_constants), tabs);
			}
		}
	}
	else
	{
		d_formulae.push_back(_root);
		_root->getFormula()->getConstants(d_constants);
		return prove(move(d_formulae), move(d_constants), tabs);
	}
}

bool Tableaux::checkIfExistsComplementaryPairOfLiterals(deque<SignedFormula>& d_formulae) const
{
	deque<SignedFormula>::const_iterator iter_outer = d_formulae.cbegin();
	for (; iter_outer != d_formulae.cend(); ++iter_outer)
	{
		// If the formula is not an atom, we can skip the check
		if ((*iter_outer)->getFormula()->getType() != BaseFormula::T_ATOM)
		{
			continue;
		}

		deque<SignedFormula>::const_iterator iter_inner = iter_outer + 1;
		for (; iter_inner != d_formulae.cend(); ++iter_inner)
		{
			// Complementary pair of literals are TX and FX, where X is a literal
			if ((*iter_outer)->getSign() != (*iter_inner)->getSign() &&
				(*iter_outer)->getFormula()->equalTo((*iter_inner)->getFormula()))
			{
				return true;
			}
		}
	}

	return false;
}

bool Tableaux::checkIfExistsNonGammaRule(deque<SignedFormula>& d_formulae, SignedFormula & rule, BaseSignedFormula::TableauxType & ruleType) const
{
	deque<SignedFormula>::const_iterator iter = d_formulae.cbegin();
	for (; iter != d_formulae.cend(); ++iter)
	{
		BaseSignedFormula::TableauxType tType = (*iter)->getType();
		if (tType == BaseSignedFormula::TT_ALPHA || tType == BaseSignedFormula::TT_BETA || tType == BaseSignedFormula::TT_DELTA)
		{
			rule = *iter;
			ruleType = tType;
			return true;
		}
	}

	return false;
}

bool Tableaux::checkIfShouldBranchBeOpenForGammaRule(deque<SignedFormula>& d_formulae, deque<FunctionSymbol> & d_constants) const
{
	deque<SignedFormula> d_gammaFormulae, d_nextFormulaeNode;
	deque<SignedFormula>::const_iterator iterFormulae = d_formulae.cbegin();
	
	for (; iterFormulae != d_formulae.cend(); ++iterFormulae)
	{
		// Extract all gamma formulae
		if ((*iterFormulae)->getType() == BaseSignedFormula::TT_GAMMA)
		{
			d_gammaFormulae.push_back(*iterFormulae);
		}
		
		// Start filling the next node queue 
		d_nextFormulaeNode.push_back(*iterFormulae);
	}
		
	// Complete filling the next node deque by instantiating gamma formulae
	iterFormulae = d_gammaFormulae.cbegin();
	for (; iterFormulae != d_gammaFormulae.cend(); ++iterFormulae)
	{
		deque<FunctionSymbol>::const_iterator iterConstants = d_constants.cbegin();
		for (; iterConstants != d_constants.cend(); ++iterConstants)
		{
			Variable v;
			BaseFormula::Type fType = (*iterFormulae)->getFormula()->getType();
			Quantifier * pQuantFormula = (Quantifier *)((*iterFormulae)->getFormula().get());
			v = pQuantFormula->getVariable();
			
			Formula instFormula = (*iterFormulae)->getFormula()->instantiate(v, make_shared<FunctionTerm>(*iterConstants));
			SignedFormula instSignedFormula = make_shared<BaseSignedFormula>(instFormula, (*iterFormulae)->getSign());
			
			deque<SignedFormula>::const_iterator iterFindFormulaeInner = find(d_nextFormulaeNode.cbegin(), d_nextFormulaeNode.cend(), instSignedFormula);
			if (iterFindFormulaeInner == d_nextFormulaeNode.cend())
			{
				d_nextFormulaeNode.push_back(instSignedFormula);
			}
		}
	}
	
	// Check if the next node deque is the same as current node deque 
	if (d_formulae.size() != d_nextFormulaeNode.size())
	{
		d_formulae = move(d_nextFormulaeNode);
		return false;
	}
	
	iterFormulae = d_formulae.cbegin();
	for (; iterFormulae != d_formulae.cend(); ++iterFormulae)
	{
		deque<SignedFormula>::const_iterator iterFindFormulae = find(d_nextFormulaeNode.cbegin(), d_nextFormulaeNode.cend(), *iterFormulae);
		if (iterFindFormulae == d_nextFormulaeNode.cend())
		{
			d_formulae = move(d_nextFormulaeNode);
			return false;
		}
	}
	
	return true;
}

bool Tableaux::atomRules(deque<SignedFormula> && d_formulae, deque<FunctionSymbol> && d_constants, const SignedFormula & f, int tabs) const
{
	deque<SignedFormula>::const_iterator iter = find(d_formulae.cbegin(), d_formulae.cend(), f);
	if (iter != d_formulae.cend())
	{
		d_formulae.erase(iter);
	}
	d_formulae.push_back(f);
	return prove(move(d_formulae), move(d_constants), tabs);
}

bool Tableaux::notRules(deque<SignedFormula>&& d_formulae, deque<FunctionSymbol> && d_constants, const SignedFormula & f, int tabs) const
{
	Not * pRule = (Not *)f->getFormula().get();
	d_formulae.push_back(make_shared<BaseSignedFormula>(pRule->getOperand(), !f->getSign()));

	deque<SignedFormula>::const_iterator iter = find(d_formulae.cbegin(), d_formulae.cend(), f);
	if (iter != d_formulae.cend())
	{
		d_formulae.erase(iter);
	}

	return prove(move(d_formulae), move(d_constants), tabs);
}

bool Tableaux::andRules(deque<SignedFormula> && d_formulae, deque<FunctionSymbol> && d_constants, const SignedFormula & f, int tabs) const
{
	// If X /\ Y is true, then X and Y are both true.
	if (f->getSign())
	{
		deque<SignedFormula>::const_iterator iter = find(d_formulae.cbegin(), d_formulae.cend(), f);
		if (iter != d_formulae.cend())
		{
			d_formulae.erase(iter);
		}
		d_formulae.push_back(make_shared<BaseSignedFormula>(((And*)f->getFormula().get())->getOperand1(), true));
		d_formulae.push_back(make_shared<BaseSignedFormula>(((And*)f->getFormula().get())->getOperand2(), true));
		return prove(move(d_formulae), move(d_constants), tabs);
	}
	// If X /\ Y is false, then either X is false or Y is false.
	else
	{
		bool res1, res2;
		std::deque<SignedFormula> tmp(d_formulae);

		// first, check what happens if X is false
		deque<SignedFormula>::const_iterator iter = find(d_formulae.cbegin(), d_formulae.cend(), f);
		if (iter != d_formulae.cend())
		{
			d_formulae.erase(iter);
		}
		d_formulae.push_back(make_shared<BaseSignedFormula>(((And*)f->getFormula().get())->getOperand1(), false));
		res1 = prove(move(d_formulae), move(d_constants), tabs + 1);
		cout << string(tabs + 1, '\t') << (res1 ? "X" : "O") << endl;

		d_formulae = tmp;

		// if the first branch is closed, then...
		if (res1)
		{
			// ... check what happens if Y is false
			deque<SignedFormula>::const_iterator iter = find(d_formulae.cbegin(), d_formulae.cend(), f);
			if (iter != d_formulae.cend())
			{
				d_formulae.erase(iter);
			}
			d_formulae.push_back(make_shared<BaseSignedFormula>(((And*)f->getFormula().get())->getOperand2(), false));
			res2 = prove(move(d_formulae), move(d_constants), tabs + 1);
			cout << string(tabs + 1, '\t') << (res2 ? "X" : "O") << endl;

			move(d_formulae) = tmp;

			// both branches have to be closed to close their superbranch
			return res1 && res2;
		}
		// if the first branch is not closed, then its superbranch cannot be closed
		else
		{
			return res1; // false
		}
	}
}

bool Tableaux::orRules(deque<SignedFormula> && d_formulae, deque<FunctionSymbol> && d_constants, const SignedFormula & f, int tabs) const
{
	// If X /\ Y is true, then either X is true or Y is true.
	if (f->getSign())
	{
		bool res1, res2;
		std::deque<SignedFormula> tmp(d_formulae);

		// first, check if X is true
		deque<SignedFormula>::const_iterator iter = find(d_formulae.cbegin(), d_formulae.cend(), f);
		if (iter != d_formulae.cend())
		{
			d_formulae.erase(iter);
		}
		d_formulae.push_back(make_shared<BaseSignedFormula>(((Or*)f->getFormula().get())->getOperand1(), true));
		res1 = prove(move(d_formulae), move(d_constants), tabs + 1);
		cout << string(tabs + 1, '\t') << (res1 ? "X" : "O") << endl;

		d_formulae = tmp;

		// if the first branch is closed, then...
		if (res1)
		{
			// ... check what happens if Y is true
			deque<SignedFormula>::const_iterator iter = find(d_formulae.cbegin(), d_formulae.cend(), f);
			if (iter != d_formulae.cend())
			{
				d_formulae.erase(iter);
			}
			d_formulae.push_back(make_shared<BaseSignedFormula>(((Or*)f->getFormula().get())->getOperand2(), true));
			res2 = prove(move(d_formulae), move(d_constants), tabs + 1);
			cout << string(tabs + 1, '\t') << (res2 ? "X" : "O") << endl;

			d_formulae = tmp;

			// both branches have to be closed to close their superbranch
			return res1 && res2;
		}
		// if the first branch is not closed, then its superbranch cannot be closed
		else
		{
			return res1; // false
		}
	}
	// If X \/ Y is false, then X and Y are both false.
	else
	{
		deque<SignedFormula>::const_iterator iter = find(d_formulae.cbegin(), d_formulae.cend(), f);
		if (iter != d_formulae.cend())
		{
			d_formulae.erase(iter);
		}
		d_formulae.push_back(make_shared<BaseSignedFormula>(((Or*)f->getFormula().get())->getOperand1(), false));
		d_formulae.push_back(make_shared<BaseSignedFormula>(((Or*)f->getFormula().get())->getOperand2(), false));
		return prove(move(d_formulae), move(d_constants), tabs);
	}
}

bool Tableaux::impRules(deque<SignedFormula> && d_formulae, deque<FunctionSymbol> && d_constants, const SignedFormula & f, int tabs) const
{
	// If X => Y is true, then either X is false or Y is true.
	if (f->getSign())
	{
		bool res1, res2;
		std::deque<SignedFormula> tmp(d_formulae);

		// first, check if X is false
		deque<SignedFormula>::const_iterator iter = find(d_formulae.cbegin(), d_formulae.cend(), f);
		if (iter != d_formulae.cend())
		{
			d_formulae.erase(iter);
		}
		d_formulae.push_back(make_shared<BaseSignedFormula>(((Imp*)f->getFormula().get())->getOperand1(), false));
		res1 = prove(move(d_formulae), move(d_constants), tabs + 1);
		cout << string(tabs + 1, '\t') << (res1 ? "X" : "O") << endl;

		d_formulae = tmp;

		// if the first branch is closed, then...
		if (res1)
		{
			// ... check what happens if Y is true
			deque<SignedFormula>::const_iterator iter = find(d_formulae.cbegin(), d_formulae.cend(), f);
			if (iter != d_formulae.cend())
			{
				d_formulae.erase(iter);
			}
			d_formulae.push_back(make_shared<BaseSignedFormula>(((Imp*)f->getFormula().get())->getOperand2(), true));
			res2 = prove(move(d_formulae), move(d_constants), tabs + 1);
			cout << string(tabs + 1, '\t') << (res2 ? "X" : "O") << endl;

			d_formulae = tmp;

			// both branches have to be closed to close their superbranch
			return res1 && res2;
		}
		// if the first branch is not closed, then its superbranch cannot be closed
		else
		{
			return res1; // false
		}
	}
	// If X => Y is false, then X is true and Y is false.
	else
	{
		deque<SignedFormula>::const_iterator iter = find(d_formulae.cbegin(), d_formulae.cend(), f);
		if (iter != d_formulae.cend())
		{
			d_formulae.erase(iter);
		}
		d_formulae.push_back(make_shared<BaseSignedFormula>(((Imp*)f->getFormula().get())->getOperand1(), true));
		d_formulae.push_back(make_shared<BaseSignedFormula>(((Imp*)f->getFormula().get())->getOperand2(), false));
		return prove(move(d_formulae), move(d_constants), tabs);
	}
}

bool Tableaux::forallRules(deque<SignedFormula> && d_formulae, deque<FunctionSymbol> && d_constants, const SignedFormula & f, int tabs) const
{
	// TODO: implement method
	return false;
}

bool Tableaux::existsRules(deque<SignedFormula> && d_formulae, deque<FunctionSymbol> && d_constants, const SignedFormula & f, int tabs) const
{
	// TODO: implement method
	return false;
}

// END Tableaux
// ----------------------------------------------------------------------------

ostream & operator<<(ostream & ostr, SignedFormula sf)
{
	sf->printSignedFormula(ostr);
	return ostr;
}

template<class T>
ostream & operator<<(ostream & ostr, deque<T> & d_T)
{
	ostr << "{ ";
	auto b = d_T.cbegin();
	auto e = d_T.cend();
	for (; b != e; b++)
	{
		ostr << *b;
		if (b + 1 != e)
		{
			ostr << ", ";
		}
	}
	ostr << " }";
	return ostr;
}