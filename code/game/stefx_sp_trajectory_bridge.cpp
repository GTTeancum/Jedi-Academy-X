#include "q_shared.h"

extern void STEFX_SP_EvaluateTrajectory( const trajectory_t *tr, int atTime, vec3_t result );

void EvaluateTrajectory( const trajectory_t *tr, int atTime, vec3_t result )
{
	STEFX_SP_EvaluateTrajectory( tr, atTime, result );
}
