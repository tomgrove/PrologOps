/*
Essentials of PROLOG operational semantics in 150 lines of C++

Well, I think its neat ....

PROLOG ( PRO-gramming in LOG-ic ), along with LISP, was one of the two dominant AI languages of the 70s and 80s. Programs are expressed a series
of terms in first order predicate calculus. More here https://en.wikipedia.org/wiki/Prolog. Also very influenced by http://wambook.sourceforge.net/  
and the overall approach taken by http://yieldprolog.sourceforge.net/ 

For example:

parent( george, fred ).
parent( george, sally ).
male( george ).

father(Person, Child ) :- male( Person ), parent( Person, Child ).

The first line asserts that george is the parent of sally, the second that george is male. The final line specifies a rule - a Person ( variables
start with an upper case character in Prolog ) is a father of a Child if Person is male and Person is a parent of  Child. So queries of the form:

?- father( george, fred)
?- father( george, sally)

would both succeed. So far so good - predicates behave very much like function calls in other languages. However, the attraction of Prolog has 
always been the built in ability to create choice points where there are multiple possible paths of execution. I.e. if one were to execute

?- parent( george, Child )
Child = fred

one would get Child bound to "fred". However, there are two matching predicates here and PROLOG creates a choice point where execution can 
be restarted in the event of failure in the future. In the example above, such a retry would result in Child being bound to sally.

This ability to rewind execution finds application is search, planning, parsing etc. 

This is kind of a cool feature - so how would o9ne express PROLOG style operational semantics in C++? 

We will start with some basic includes - <functional> is the important one

*/

#include <functional>
#include <vector>

/* 
Next we define a couple of basic types to represent data types and forward declare the Term type
*/

struct Term;

enum Type
{
	eVariable,
	eAtom
};

/* 
There are two types. The first of these is the Variable. In PROLOG, these are designated in the source by names starting with an uppercase char. E.g.

Parent
Child
etc.

 
*/

struct Variable
{
	bool	mIsBound;
	Term*	mReference;
};

/* 
If mIsBound is true, then mReference points to another Term. In classic implementations, unbound variables are more efficiently expressed by having them refer to themselves, but here using
an explicit flag. The other type is an Atom: 
*/

struct Atom
{
	char*	mName;
	int		mArity;
	Term*	mTerms[10];
};

/* 
A bit scruffy with that 10, but the idea is that an Atom describes a collection of Terms E.g. the following are Atoms

	george
	male( george )
	parent( george, sally)
	[ one, two, three four ]
	[ H | T ]
	coord( X, Y )
	
the number of arguments is given by the mArity term. Lists ( and trees, etc. ) can be expressed as nested pairs:
	
	.( one, .( two, .( three. [] )))
	.( H, T)
	
A Term is simply defined as a union of these two types ( and could be expressed better with boost::any or a discriminated union library, but would be overkill here ) 
*/

struct Term
{
	Type	mType;
	union 
	{
		Variable	mVariable;
		Atom		mAtom;
	};
};

/* 
So this gives us a very hacky and minimal way of expressing PROLOG's data structures. Everything is a Term - a dynamically typed element. There are two types of Terms are
being considered here - Variables and Atoms. "Real" PROLOG, of course, has integers, floating point numbers and other types, but this is enough to apply the operational behaviour. 
The key operation in PROLOG is unification. This matches two Terms, taking unbound variables in each of the Terms and binding them to Atoms and Variables in the other Term. To 
support this, we define a function Deref. In the case of a bound variable this will follow the chain of references until it hits either an Atom or an unbound variable: 
*/

Term* Deref(Term* Root)
{
	Term* t = Root;
	while (t->mType == eVariable && t->mVariable.mIsBound)
	{
		t = t->mVariable.mReference;
	}

	return t;
}

/* 
We now define two std::functions. Together these are used to implement the operational semantics of PROLOG. 
*/

typedef std::function<void(void)>	Retry;
typedef std::function<void(Retry)>	Continuation;

/* 
To illustrate how these are used, consider the definition of Unify 
*/

void Unify(Term* t0, Term* t1, Continuation K, Retry R);

/*  
We express the code in Continuation Passing Style: https://en.wikipedia.org/wiki/Continuation-passing_style. This is a powerful
idea that lets us customise the control flow of of our program without resorting to "goto" or other risky and platform sensitive
features. The fundamental idea that one can take a conventional program:

int x =  DoWork()
float 	 DoMoreWork( x, 20 );

and rewrite it:

DoWork( []( int x ){
DoMoreWork( x, 20, []( float ){ .... } )})

where each function now returns void, but has an additional argument that holds a continuation. E.g.

DoWork -> DoWork( Continuation K )

The continuations are expressed as lambdas in this case. Why would you do this? The main motivation is to assert control over the
program flow. This could be to add co-routine support or suspend execution until some potentially long running operation has completed. 

implementing PROLOG style semantics goes slightly further - in addition to having a continuation that we execute if unification succeeds, we
also have a Retry that we execute if unification fails. This captures the state of the program at some earlier point. To support this, we also
have another structure called the "Trail":

Alternatively, can think of this as addressing the concept of  multiple continuations. I.e. what if the program is indeterministic and there
are multiple possible continuations. This then stores the following choice of continuation. 

*/

struct Trail
{
	std::vector<Term*>	mTrail;
	void Add(Term* t )
	{
		mTrail.push_back(t);
	}

	void UnWind( int Index)
	{
		while (mTrail.size()  != Index)
		{
			mTrail.back()->mVariable.mIsBound = false;
			mTrail.pop_back();
		}
	}
};

Trail gTrail;

/* 
The trail records the binding history of terms. As terms are bound, we add them to this trail ( the term is taken from Warren's Abstract Machine
http://wambook.sourceforge.net/ ). I admit that when I have tried to write this before, I avoided this structure and tried to have all the state
captured inside the lambda's. But this is painful. Studying and it is simply more straightforward to capture a restore point and roll back to it -
PROLOG like other functional languages cannot mutate variables other than through binding so this is really the only operation that has be undone
when we return to an earlier point of execution.  

Probably a good time to mention I have made absolutely no attempt at expressing variable lifetimes, so the trail will in general grow over time. 
If one imagines something like:


loop(X) :- write(X),
		   NewX is X + 1,
		   !, 
		   loop(NewX).
		   
Each iteration will construct a new variable, even with the '!' which is the cut construct. 'cut' in PROLOG clears the Retry and prevents
further backtracking. 

Bind has the obvious definition:
*/ 

void Bind(Term* t0, Term* t1)
{
	t0->mVariable.mReference = t1;
	t0->mVariable.mIsBound = true;
	gTrail.Add(t0);
}

/* 
When we want to restore the status of terms to an earlier point, we call UnWind() and unbind all the Terms from before this point. Now for
the definition of Unify: 
*/ 

void UnifyTerms(Term** t0s, Term** t1s, Continuation K, Retry R, int Arity);

void Unify(Term* t0, Term* t1, Continuation K, Retry R)
{
	Term* t0dr = Deref(t0);
	Term* t1dr = Deref(t1);

	if (t0dr->mType == eVariable)
	{
		Bind(t0dr, t1dr);
		K(R);
	}
	else if (t1dr->mType == eVariable)
	{
		Bind(t1dr, t0dr);
		K(R);
	}
	else if (strcmp(t1dr->mAtom.mName, t0dr->mAtom.mName) == 0 &&
		t1dr->mAtom.mArity == t0dr->mAtom.mArity)
	{
		UnifyTerms(t0dr->mAtom.mTerms, t1dr->mAtom.mTerms, K, R, t0dr->mAtom.mArity);
	}
	else
	{
		R();
	}
}

/* 
 So, given two terms they are first dereferenced ( Deref ). If either term is an unbound  variable, it is bound to the other, and we continue. If the
 terms are both Atoms, their terms are matched,  providing the predicate name and arity match. If this is the case, then we backtrack to 
 an earlier state by calling retry. Unify terms calls unify on each of the sub-terms. If the Arity is 0, then we have successfully unified the 
 terms and we continue. Otherwise, we create a backtrack point to follow if we fail to unify the next term. This captures the current
 trail index. In this case, this retry will simply have the effect of unwinding the unification.  
 */
 
 void UnifyTerms(Term** t0s, Term** t1s, Continuation K, Retry R, int Arity)
{
	if (Arity == 0)
	{
		K(R);
	}
	else
	{
		int index = gTrail.mTrail.size();
		auto r = [R, index]() {
			gTrail.UnWind(index);
			R();
		};

		auto k = [Arity, K,r, t0s, t1s ](Retry R) {
			UnifyTerms(t0s + 1, t1s + 1, K, r, Arity - 1);
		};

		Unify(*t0s, *t1s, k, r);
	}
}
 
/* 

Taken with Unify(), UnifyTerms() is tail recursive - this is a side effect of CPS, as no function ever returns - it simply calls its continuation. In theory, this means that no stack space is
required for the recursive call and it can be simply turned into a jmp. This isn't true in C++, though. Consider:


void loop( int x, std::function<void(int)> Continuation )
{
	printf("count: %d\n", x );
	Continuation( x );
}

:
loop( 0, [](int x ){ loop( x ); } );

which should be an infinite loop. However, it will eventually run out of stack space. C++ is bad at identifying TR opportunities, but in this case it is simply because loop is not really TR - the call to
the continuation is followed by running the destructor for the std::function. A solution is to truncate the stack recursion, by doing something like:


loop( 0, []( int x) { gCont = [x](){ loop(x); }});
while( gCont != nullptr)
{
	gCont();
}

And that, is basically, that - 150 lines without comments. With these definitions you can effectively implement PROLOG-like operational semantics in C++. Some utility functions: 
*/

Term* mkVar()
{
	auto v = new Term();
	v->mType = eVariable;
	v->mVariable.mIsBound = false;
	v->mVariable.mReference = nullptr;
	return v;
}

Term* mkAtom(char* Name)
{
	auto a = new Term();
	a->mType = eAtom;
	a->mAtom.mName = Name;
	a->mAtom.mArity = 0;
	return a;
}

Term* mkAtom(char* Name, Term* a0)
{
	auto a = new Term();
	a->mType = eAtom;
	a->mAtom.mName = Name;
	a->mAtom.mArity = 1;
	a->mAtom.mTerms[0] = a0;
	return a;
}

/* 
okay, could have used variable args here - cut n paste laziness! 
*/

Term* mkAtom(char* Name, Term* a0, Term* a1)
{
	auto a = new Term();
	a->mType = eAtom;
	a->mAtom.mName = Name;
	a->mAtom.mArity = 2;
	a->mAtom.mTerms[0] = a0;
	a->mAtom.mTerms[1] = a1;
	return a;
}

/* 
And a more detailed example:

So lets take the standard PROLOG built-in predicate "member":
	
	member( H, [H|_] ).
	member( A, [_|T] ) :- member( A,T ).
	
	( _ is a wild card )
	
This predicate takes two arguments and will succeed if the first argument is a member of the list specified by the second argument. E.g:

	member( cat, [ dog, cat, frog ] ).
	
But also:

	member( Item, [ dog, cat, frog ] ).
	
will initially succeed with Item bound to dog. If we retry, Item will be rebound to cat, then frog and finally fail, having exhausted all
options. Here's a definition of member. We start with the definition of the general case:

	member( A0, [_|T] ) :- member( A0,T ).
	
*/
void Member0(Term* Item, Term* List, Continuation K, Retry R);

void Member1(Term* Item, Term* List, Continuation K, Retry R)
{
	Term* A0 = Item;							// A0	
	Term* H = mkVar();
	Term* T = mkVar();
	Term* A1 = mkAtom(".", H, T);				// A1 = [_|T]
	
/*
	The first block of code matches the arguments against the predicates templates
*/
	
	int index = gTrail.mTrail.size();
	auto r = [index, Item, List, K, R]() {
		gTrail.UnWind(index);					
		R();
	};
	
/* 
	now construct a retry. This is the "last" instance of the two member predicates, so retrying will just call the passed in rety 
*/

	auto k = [K, A0, T](Retry R)
	{
		Member0(A0, T, K, R);
	};

/* 
	the continuation holds the body, which calls the other instance of member 
*/

	Unify(List, A1, k, r);
	
/*	
	The call unify matches the passed in list against the template built in A1. If this match succeeds - which it will do unless we
	have reached the end of the list - execute the body which will resume with the first instance of member ( Member0 )
*/ 
 
}

/*  
	The more specific clause is:

	member( A0, [A0|_]).
	
*/	

void Member0(Term* Item, Term* List, Continuation K, Retry R)
{
	Term* A0 = Item;
	Term* A1 = mkAtom(".",Item, mkVar());

/*  
	Again, construct the argument template for matching 
*/	

	int index = gTrail.mTrail.size();
	auto r = [index, Item, List, K, R]() {
		gTrail.UnWind(index);
		Member1(Item, List, K, R);
	};

/* 
	In this case, there is an alternative instance of member ( Member1 ) which we will execute if this fails 
*/

	auto k = [K](Retry R)
	{
		K(R);
	};

/* 
	This body-less, so simply executes the passed in continuation 
*/
	
	Unify(List, A1, k, r);
	
/* 
	Unify matches the List against the template. This will succeed if the head of the list matches the Item. If it does not, retry
	will proceed to the alternative definition of member. Other execution continues with the code passed in via K 
*/
}

/* A basic term printing utiliy */	
	
void Print(Term* Root)
{
	Term* t = Deref(Root);
	if (t->mType == eAtom)
	{
		printf("%s(", t->mAtom.mName);
		for (int i = 0; i < t->mAtom.mArity; i++)
		{
			Print(t->mAtom.mTerms[i]);
		}
		printf(")");
	}
	else
	{
		printf("X?");
	}
}

/* 
An illustration. This performs:

	member( Item, [ cat, dog, frog ]),
	member( Item, [ cat, monkey, frog ]).
	
This binds Item to list members common to both lists. In this case it prints cat first, followed by from

*/


int main()
{
	Term* list =  mkAtom(".", mkAtom("cat"), mkAtom(".", mkAtom("dog"),    mkAtom(".", mkAtom("frog"), mkAtom("[]"))));
	Term* list2 = mkAtom(".", mkAtom("cat"), mkAtom(".", mkAtom("monkey"), mkAtom(".", mkAtom("frog"), mkAtom("[]"))));
	Term* item =  mkVar(); 

	Member0(item, list, [item, list2](Retry R) {
		Member0(item, list2, [item](Retry R) { Print(item); R(); }, R); },
		[]() {});

    return 0;
}
