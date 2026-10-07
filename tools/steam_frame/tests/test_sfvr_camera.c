#include "sfvr/sfvr_camera.h"
#include "sfvr_test.h"

static void test_zero_preserves_source_and_positive_preserves_old_math(void)
{
	const float source[3] = { 10.0f, 20.0f, -30.0f };
	const float target[3] = { 30.0f, 0.0f, 10.0f };
	float out[3];

	sfvr_camera_origin(source, target, 0.0f, 0.0f, out);
	for (int i = 0; i < 3; i++)
		CHECK_NEAR(out[i], source[i], 1e-6);

	sfvr_camera_origin(source, target, 0.5f, 0.0f, out);
	CHECK_NEAR(out[0], 20.0, 1e-6);
	CHECK_NEAR(out[1], 10.0, 1e-6);
	CHECK_NEAR(out[2], -10.0, 1e-6);

	sfvr_camera_origin(source, target, 0.0f, 0.5f, out);
	CHECK_NEAR(out[0], 10.0, 1e-6);
	CHECK_NEAR(out[1], 10.0, 1e-6);
	CHECK_NEAR(out[2], -30.0, 1e-6);
}

static void test_signed_extension_and_range_clamping(void)
{
	const float source[3] = { 0.0f, 10.0f, 0.0f };
	const float target[3] = { 0.0f, 0.0f, 0.0f };
	float out[3];

	sfvr_camera_origin(source, target, -1.0f, 0.0f, out);
	CHECK_NEAR(out[1], 20.0, 1e-6); /* farther from the target */
	sfvr_camera_origin(source, target, 0.0f, -1.0f, out);
	CHECK_NEAR(out[1], 20.0, 1e-6); /* higher than the source */
	sfvr_camera_origin(source, target, -5.0f, 4.0f, out);
	CHECK_NEAR(out[1], 0.0, 1e-6); /* clamped to closer=-1, lower=1: target height */
	sfvr_camera_origin(source, target, 2.0f, -2.0f, out);
	CHECK_NEAR(out[1], 4.0, 1e-6); /* closer=.8, lower=-1 preserve signed formula */
}

int main(void)
{
	RUN(test_zero_preserves_source_and_positive_preserves_old_math);
	RUN(test_signed_extension_and_range_clamping);
	return SFVR_TEST_RESULT("test_camera");
}
