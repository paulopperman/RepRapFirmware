/*
 * ZLeadscrewKinematics.cpp
 *
 *  Created on: 8 Jul 2017
 *      Author: David
 */

#include "ZLeadscrewKinematics.h"

#include <Platform/RepRap.h>
#include <Platform/Platform.h>
#include <Movement/Move.h>
#include <GCodes/GCodeBuffer/GCodeBuffer.h>

const float M3ScrewPitch = 0.5;

#if SUPPORT_OBJECT_MODEL

// Object model table and functions
// Note: if using GCC version 7.3.1 20180622 and lambda functions are used in this table, you must compile this file with option -std=gnu++17.
// Otherwise the table will be allocated in RAM instead of flash, which wastes too much RAM.

// Macro to build a standard lambda function that includes the necessary type conversions
#define OBJECT_MODEL_FUNC(...) OBJECT_MODEL_FUNC_BODY(ZLeadscrewKinematics, __VA_ARGS__)

constexpr ObjectModelArrayDescriptor ZLeadscrewKinematics::lastCorrectionsArrayDescriptor =
{
	nullptr,					// no lock needed
	[] (const ObjectModel *self, const ObjectExplorationContext&) noexcept -> size_t { return ((const ZLeadscrewKinematics*)self)->numLeadscrews; },
	[] (const ObjectModel *self, ObjectExplorationContext& context) noexcept -> ExpressionValue
									{ return ExpressionValue(((const ZLeadscrewKinematics*)self)->lastCorrections[context.GetLastIndex()], 3); }
};

constexpr ObjectModelArrayDescriptor ZLeadscrewKinematics::screwXArrayDescriptor =
{
	nullptr,					// no lock needed
	[] (const ObjectModel *self, const ObjectExplorationContext&) noexcept -> size_t { return ((const ZLeadscrewKinematics*)self)->numLeadscrews; },
	[] (const ObjectModel *self, ObjectExplorationContext& context) noexcept -> ExpressionValue
									{ return ExpressionValue(((const ZLeadscrewKinematics*)self)->leadscrewX[context.GetLastIndex()], 1); }
};

constexpr ObjectModelArrayDescriptor ZLeadscrewKinematics::screwYArrayDescriptor =
{
	nullptr,					// no lock needed
	[] (const ObjectModel *self, const ObjectExplorationContext&) noexcept -> size_t { return ((const ZLeadscrewKinematics*)self)->numLeadscrews; },
	[] (const ObjectModel *self, ObjectExplorationContext& context) noexcept -> ExpressionValue
									{ return ExpressionValue(((const ZLeadscrewKinematics*)self)->leadscrewY[context.GetLastIndex()], 1); }
};

constexpr ObjectModelTableEntry ZLeadscrewKinematics::objectModelTable[] =
{
	// Within each group, these entries must be in alphabetical order
	// 0. kinematics members
	{ "tiltCorrection",		OBJECT_MODEL_FUNC(self, 1), 								ObjectModelEntryFlags::none },

	// 1. tiltCorrection members
	{ "correctionFactor",	OBJECT_MODEL_FUNC(self->correctionFactor, 1), 				ObjectModelEntryFlags::none },
	{ "lastCorrections",	OBJECT_MODEL_FUNC_NOSELF(&lastCorrectionsArrayDescriptor), 	ObjectModelEntryFlags::none },
	{ "maxCorrection",		OBJECT_MODEL_FUNC(self->maxCorrection, 1), 					ObjectModelEntryFlags::none },
	{ "screwPitch",			OBJECT_MODEL_FUNC(self->screwPitch, 2), 					ObjectModelEntryFlags::none },
	{ "screwX",				OBJECT_MODEL_FUNC_NOSELF(&screwXArrayDescriptor), 			ObjectModelEntryFlags::none },
	{ "screwY",				OBJECT_MODEL_FUNC_NOSELF(&screwYArrayDescriptor), 			ObjectModelEntryFlags::none },
};

constexpr uint8_t ZLeadscrewKinematics::objectModelTableDescriptor[] = { 2, 1, 6 };

DEFINE_GET_OBJECT_MODEL_TABLE_WITH_PARENT(ZLeadscrewKinematics, Kinematics)

#endif

ZLeadscrewKinematics::ZLeadscrewKinematics(KinematicsType k) noexcept
	: Kinematics(k, SegmentationType(false, false, false)), numLeadscrews(0), correctionFactor(1.0), maxCorrection(1.0), screwPitch(M3ScrewPitch)
{
}

ZLeadscrewKinematics::ZLeadscrewKinematics(KinematicsType k, SegmentationType segType) noexcept
	: Kinematics(k, segType), numLeadscrews(0), correctionFactor(1.0), maxCorrection(1.0), screwPitch(M3ScrewPitch)
{
}

// Configure this kinematics. We only deal with the leadscrew coordinates here
bool ZLeadscrewKinematics::Configure(unsigned int mCode, GCodeBuffer& gb, const StringRef& reply, bool& error) THROWS(GCodeException)
{
	if (mCode == 671 && GetKinematicsType() != KinematicsType::coreXZ)
	{
		// Configuring leadscrew positions.
		// We no longer require the number of leadscrews to equal the number of motors. If there is a mismatch then auto calibration just prints the corrections.
		// We now allow just 1 pair of coordinates to be specified, which in effect clears the M671 settings.
		bool seenX = false, seenY = false;
		size_t xSize = MaxLeadscrews, ySize = MaxLeadscrews;
		if (gb.Seen('X'))
		{
			gb.GetFloatArray(leadscrewX, xSize, false);
			seenX = true;
		}
		if (gb.Seen('Y'))
		{
			gb.GetFloatArray(leadscrewY, ySize, false);
			seenY = true;
		}

		bool seenPFS = false;
		gb.TryGetFValue('S', maxCorrection, seenPFS);
		gb.TryGetFValue('P', screwPitch, seenPFS);
		gb.TryGetFValue('F', correctionFactor, seenPFS);

		if (seenX && seenY && xSize == ySize)
		{
			for (float& v : lastCorrections)
			{
				v = 0.0;
			}
			numLeadscrews = xSize;
			reprap.MoveUpdated();
			return false;							// successful configuration
		}

		if (seenX || seenY)
		{
			reply.copy("Specify 1, 2, 3 or 4 X and Y coordinates in M671");
			return true;
		}

		// If no parameters provided so just report the existing setup
		if (seenPFS)
		{
			reprap.MoveUpdated();
			return true;							// just changed the maximum correction or screw pitch
		}
		else if (numLeadscrews < 2)
		{
			reply.copy("Z leadscrew coordinates are not configured");
		}
		else
		{
			reply.copy("Z leadscrew coordinates");
			for (unsigned int i = 0; i < numLeadscrews; ++i)
			{
				reply.catf(" (%.1f,%.1f)", (double)leadscrewX[i], (double)leadscrewY[i]);
			}
			reply.catf(", factor %.02f, maximum correction %.02fmm, manual adjusting screw pitch %.02fmm", (double)correctionFactor, (double)maxCorrection, (double)screwPitch);
		}
		return false;
	}
	return Kinematics::Configure(mCode, gb, reply, error);
}

// Return true if the kinematics supports auto calibration based on bed probing.
bool ZLeadscrewKinematics::SupportsAutoCalibration() const noexcept
{
	return numLeadscrews >= 2;
}

// Perform auto calibration, returning true if failed. Override this implementation in kinematics that support it. Caller already owns the GCode movement lock.
bool ZLeadscrewKinematics::DoAutoCalibration(size_t numFactors, const RandomProbePointSet& probePoints, const StringRef& reply) noexcept
{
	if (!SupportsAutoCalibration())			// should be checked by caller, but check it here too
	{
		return false;
	}

	if (numFactors != numLeadscrews)
	{
		reply.printf("Number of calibration factors (%u) not equal to number of leadscrews (%u)", numFactors, numLeadscrews);
		return true;
	}

	const size_t numPoints = probePoints.NumberOfProbePoints();

	// Build a N x 2, 3 or 4 matrix of derivatives with respect to the leadscrew adjustments
	// See the wxMaxima documents for the maths involved
	FixedMatrix<floatc_t, MaxCalibrationPoints, MaxLeadscrews> derivativeMatrix;
	Deviation initialDeviation;

	{
		floatc_t initialSum = 0.0, initialSumOfSquares = 0.0;
		for (size_t i = 0; i < numPoints; ++i)
		{
			float x, y;
			const floatc_t zp = reprap.GetMove().GetProbeCoordinates(i, x, y, false);
			initialSum += zp;
			initialSumOfSquares += fcsquare(zp);

			switch (numFactors)
			{
			case 2:
				{
					const float &x0 = leadscrewX[0], &x1 = leadscrewX[1];
					const float &y0 = leadscrewY[0], &y1 = leadscrewY[1];
					// There are lot of common subexpressions in the following, but the optimiser should find them
					const floatc_t d2 = fcsquare(x1 - x0) + fcsquare(y1 - y0);
					derivativeMatrix(i, 0) = -(fcsquare(y1) - (floatc_t)(y0*y1) - (floatc_t)(y*(y1 - y0)) + fcsquare(x1) - (floatc_t)(x0*x1) - (floatc_t)(x*(x1 - x0)))/d2;
					derivativeMatrix(i, 1) = -(fcsquare(y0) - (floatc_t)(y0*y1) + (floatc_t)(y*(y1 - y0)) + fcsquare(x0) - (floatc_t)(x0*x1) + (floatc_t)(x*(x1 - x0)))/d2;
				}
				break;

			case 3:
				{
					const float &x0 = leadscrewX[0], &x1 = leadscrewX[1], &x2 = leadscrewX[2];
					const float &y0 = leadscrewY[0], &y1 = leadscrewY[1], &y2 = leadscrewY[2];
					const floatc_t d2 = x1*y2 - x0*y2 - x2*y1 + x0*y1 + x2*y0 - x1*y0;
					derivativeMatrix(i, 0) = -(floatc_t)(x1*y2 - x*y2 - x2*y1 + x*y1 + x2*y - x1*y)/d2;
					derivativeMatrix(i, 1) = (floatc_t)(x0*y2 - x*y2 - x2*y0 + x*y0 + x2*y - x0*y)/d2;
					derivativeMatrix(i, 2) = -(floatc_t)(x0*y1 - x*y1 - x1*y0 + x*y0 + x1*y - x0*y)/d2;
				}
				break;

			case 4:
				{
					// This one is horribly complicated. Hopefully the compiler will pick out all the common subexpressions.
					// It may not work on the older Duets that use single-precision maths, due to rounding error.
					const float &x0 = leadscrewX[0], &x1 = leadscrewX[1], &x2 = leadscrewX[2], &x3 = leadscrewX[3];
					const float &y0 = leadscrewY[0], &y1 = leadscrewY[1], &y2 = leadscrewY[2], &y3 = leadscrewY[3];

					const floatc_t x01 = x0 * x1;
					const floatc_t x02 = x0 * x2;
					const floatc_t x03 = x0 * x3;
					const floatc_t x12 = x1 * x2;
					const floatc_t x13 = x1 * x3;
					const floatc_t x23 = x2 * x3;

					const floatc_t y01 = y0 * y1;
					const floatc_t y02 = y0 * y2;
					const floatc_t y03 = y0 * y3;
					const floatc_t y12 = y1 * y2;
					const floatc_t y13 = y1 * y3;
					const floatc_t y23 = y2 * y3;

					const floatc_t d2 =   x13*y23 - x03*y23 - x12*y23 + x02*y23 - x23*y13 + x03*y13 + x12*y13 - x01*y13
										+ x23*y03 - x13*y03 - x02*y03 + x01*y03 + x23*y12 - x13*y12 - x02*y12 + x01*y12
										- x23*y02 + x03*y02 + x12*y02 - x01*y02 + x13*y01 - x03*y01 - x12*y01 + x02*y01;

					const floatc_t xx0 = x * x0;
					const floatc_t xx1 = x * x1;
					const floatc_t xx2 = x * x2;
					const floatc_t xx3 = x * x3;

					const floatc_t yy0 = y * y0;
					const floatc_t yy1 = y * y1;
					const floatc_t yy2 = y * y2;
					const floatc_t yy3 = y * y3;

					derivativeMatrix(i, 0) = - (  x13*y23 - xx3*y23 - x12*y23 + xx2*y23 - x23*y13 + xx3*y13 + x12*y13 - xx1*y13
												+ x23*yy3 - x13*yy3 - xx2*yy3 + xx1*yy3 + x23*y12 - x13*y12 - xx2*y12 + xx1*y12
												- x23*yy2 + xx3*yy2 + x12*yy2 - xx1*yy2 + x13*yy1 - xx3*yy1 - x12*yy1 + xx2*yy1
											   )/d2;
					derivativeMatrix(i, 1) =   (  x03*y23 - xx3*y23 - x02*y23 + xx2*y23 - x23*y03 + xx3*y03 + x02*y03 - xx0*y03
												+ x23*yy3 - x03*yy3 - xx2*yy3 + xx0*yy3 + x23*y02 - x03*y02 - xx2*y02 + xx0*y02
												- x23*yy2 + xx3*yy2 + x02*yy2 - xx0*yy2 + x03*yy0 - xx3*yy0 - x02*yy0 + xx2*yy0
											   )/d2;
					derivativeMatrix(i, 2) = - (  x03*y13 - xx3*y13 - x01*y13 + xx1*y13 - x13*y03 + xx3*y03 + x01*y03 - xx0*y03
												+ x13*yy3 - x03*yy3 - xx1*yy3 + xx0*yy3 + x13*y01 - x03*y01 - xx1*y01 + xx0*y01
												- x13*yy1 + xx3*yy1 + x01*yy1 - xx0*yy1 + x03*yy0 - xx3*yy0 - x01*yy0 + xx1*yy0
											   )/d2;
					derivativeMatrix(i, 3) =   (  x02*y12 - xx2*y12 - x01*y12 + xx1*y12 - x12*y02 + xx2*y02 + x01*y02 - xx0*y02
												+ x12*yy2 - x02*yy2 - xx1*yy2 + xx0*yy2 + x12*y01 - x02*y01 - xx1*y01 + xx0*y01
												- x12*yy1 + xx2*yy1 + x01*yy1 - xx0*yy1 + x02*yy0 - xx2*yy0 - x01*yy0 + xx1*yy0
											   )/d2;
				}
				break;
			}
		}

		initialDeviation.Set(initialSumOfSquares, initialSum, numPoints);
	}

	// Set up the initial and final deviations now in case calibration fails
	reprap.GetMove().SetInitialCalibrationDeviation(initialDeviation);
	reprap.GetMove().SetLatestCalibrationDeviation(initialDeviation, 0);

	if (reprap.Debug(moduleMove))
	{
		PrintMatrix("Derivative matrix", derivativeMatrix, numPoints, numFactors);
	}

	// Now build the normal equations for least squares fitting
	FixedMatrix<floatc_t, MaxLeadscrews, MaxLeadscrews + 1> normalMatrix;
	for (size_t i = 0; i < numFactors; ++i)
	{
		for (size_t j = 0; j < numFactors; ++j)
		{
			floatc_t temp = derivativeMatrix(0, i) * derivativeMatrix(0, j);
			for (size_t k = 1; k < numPoints; ++k)
			{
				temp += derivativeMatrix(k, i) * derivativeMatrix(k, j);
			}
			normalMatrix(i, j) = temp;
		}
		floatc_t temp = derivativeMatrix(0, i) * -((floatc_t)probePoints.GetZHeight(0));
		for (size_t k = 1; k < numPoints; ++k)
		{
			temp += derivativeMatrix(k, i) * -((floatc_t)probePoints.GetZHeight(k));
		}
		normalMatrix(i, numFactors) = temp;
	}

	if (reprap.Debug(moduleMove))
	{
		PrintMatrix("Normal matrix", normalMatrix, numFactors, numFactors + 1);
	}

	if (!normalMatrix.GaussJordan(numFactors, numFactors + 1))
	{
		reply.copy("Unable to calculate screw corrections. Please choose different probe points.");
		return true;
	}

	floatc_t solution[MaxLeadscrews];
	for (size_t i = 0; i < numFactors; ++i)
	{
		solution[i] = normalMatrix(i, numFactors);
	}

	if (reprap.Debug(moduleMove))
	{
		PrintMatrix("Solved matrix", normalMatrix, numFactors, numFactors + 1);
		PrintVector("Solution", solution, numFactors);
	}

	// Calculate and display the residuals, also check for errors
	Deviation finalDeviation;

	{
		floatc_t residuals[MaxCalibrationPoints];
		floatc_t finalSum = 0.0, finalSumOfSquares = 0.0;
		for (size_t i = 0; i < numPoints; ++i)
		{
			residuals[i] = probePoints.GetZHeight(i);
			for (size_t j = 0; j < numFactors; ++j)
			{
				residuals[i] += solution[j] * derivativeMatrix(i, j);
			}
			finalSum += residuals[i];
			finalSumOfSquares += fcsquare(residuals[i]);
		}

		finalDeviation.Set(finalSumOfSquares, finalSum, numPoints);

		if (reprap.Debug(moduleMove))
		{
			PrintVector("Residuals", residuals, numPoints);
		}
	}

	// Check that the corrections are sensible
	bool haveNaN = false, haveLargeCorrection = false;
	for (size_t i = 0; i < numFactors; ++i)
	{
		if (std::isnan(solution[i]))
		{
			haveNaN = true;
		}
		else
		{
			solution[i] *= (floatc_t)correctionFactor;
			if (fabsf(solution[i]) > maxCorrection)
			{
				haveLargeCorrection = true;
			}
		}
	}

	bool failed = false;
	if (haveNaN)
	{
		reply.printf("Calibration failed, computed corrections:");
		AppendCorrections(solution, reply);
		failed = true;
	}
	else
	{
		const size_t numZDrivers = reprap.GetPlatform().GetAxisDriversConfig(Z_AXIS).numDrivers;
		if (numZDrivers == numLeadscrews)
		{
			if (haveLargeCorrection)
			{
				reply.printf("Some computed corrections exceed configured limit of %.02fmm:", (double)maxCorrection);
				AppendCorrections(solution, reply);
				failed = true;
			}
			else
			{
				reprap.GetMove().AdjustLeadscrews(solution);  // TODO: This is where the correction matrix defined by solution is applied.
				for (size_t i = 0; i < numLeadscrews; ++i)
				{
					lastCorrections[i] = solution[i];
				}

				reply.printf("Leadscrew adjustments made:");
				AppendCorrections(solution, reply);

				reprap.GetMove().SetLatestCalibrationDeviation(finalDeviation, numFactors);
				reply.catf(", points used %d, (mean, deviation) before (%.3f, %.3f) after (%.3f, %.3f)",
							numPoints,
							(double)initialDeviation.GetMean(), (double)initialDeviation.GetDeviationFromMean(),
							(double)finalDeviation.GetMean(), (double)finalDeviation.GetDeviationFromMean());
			}
		}
		else
		{
			// User wants manual corrections for bed levelling screws. Leave the first one alone.
			reply.printf("Manual corrections required:");
			for (size_t i = 0; i < numLeadscrews; ++i)
			{
				const float netAdjustment = solution[i] - solution[0];
				lastCorrections[i] = netAdjustment;
				reply.catf(" %.2f turn %s (%.2fmm)", (double)(fabsf(netAdjustment)/screwPitch), (netAdjustment > 0) ? "down" : "up", (double)netAdjustment);
			}
		}
	}
	reprap.GetPlatform().MessageF(LogWarn, "%s\n", reply.c_str());
	return failed;
}

// Append the list of leadscrew corrections to 'reply'
void ZLeadscrewKinematics::AppendCorrections(const floatc_t corrections[], const StringRef& reply) const noexcept
{
	for (size_t i = 0; i < numLeadscrews; ++i)
	{
		reply.catf(" %.3f", (double)corrections[i]);
	}
}

#if HAS_MASS_STORAGE || HAS_SBC_INTERFACE

// Write any calibration data that we need to resume a print after power fail, returning true if successful
bool ZLeadscrewKinematics::WriteResumeSettings(FileStore *f) const noexcept
{
	//TODO we could write leadscrew corrections here, but they may not be the same as before
	return true;
}

#endif

// End
