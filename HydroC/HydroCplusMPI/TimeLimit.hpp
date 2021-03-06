//
// (C) Guillaume.Colin-de-Verdiere at CEA.Fr
//
#ifndef TIMELIMIT_H
#define TIMELIMIT_H
//
#include <iostream>
#include <cstring>
#include <cstdlib>
#include <cassert>
#include <cerrno>
#include <stdint.h>             // for the definition of uint32_t

// template <typename T>
class TimeLimit {
private:
	double m_orgTime;
	double m_curTime;
	double m_allotedTime;

protected:
public:
	// basic constructor
	TimeLimit(void); // default constructor. make it private if needed.
	// TimeLimit();
	// destructor
	~TimeLimit();
	double timeRemain();
	double timeRemainAll();
	// copy operator
	// TimeLimit(const TimeLimit & obj);
	// assignment operator
	// TimeLimit & operator=(const TimeLimit & rhs);
	// access through ()
	// TimeLimit & operator() (uint32_t i) ; // lhs
	// TimeLimit & operator() (uint32_t i) const ; // rhs
};
#endif
//EOF
