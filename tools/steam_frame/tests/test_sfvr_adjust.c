#include "sfvr/sfvr_adjust.h"
#include "sfvr_test.h"

#include <math.h>

static sfvr_adjust_config config(void)
{
	sfvr_adjust_config c;
	sfvr_adjust_config_default(&c);
	c.world_scale_min = 20.0f;
	c.world_scale_max = 400.0f;
	return c;
}

static float run_scale_rate(double hz, float stick_x)
{
	sfvr_adjust_config c = config();
	sfvr_adjust_state s;
	sfvr_adjust_init(&s);
	float scale = 104.0f, closer = 0.0f, lower = 0.0f;
	float stick[2] = { stick_x, 0.0f };
	int frames = (int)hz;
	for (int i = 0; i <= frames; i++) {
		stick[0] = stick_x;
		sfvr_adjust_update(&s, &c, stick, 1, 1, 0, 0, i / hz, &scale, &closer, &lower);
		CHECK_NEAR(stick[0], 0.0, 1e-6);
	}
	return scale;
}

static void test_default_config(void)
{
	sfvr_adjust_config c = config();
	CHECK_NEAR(c.deadzone, 0.20, 1e-6);
	CHECK_NEAR(c.world_size_factor_per_s, 1.5, 1e-6);
	CHECK_NEAR(c.camera_units_per_s, 0.25, 1e-6);
	CHECK_NEAR(c.max_dt_s, 1.0 / 30.0, 1e-7);
	CHECK_NEAR(c.camera_closer_min, -1.0, 1e-6);
	CHECK_NEAR(c.camera_closer_max, 0.8, 1e-6);
	CHECK_NEAR(c.camera_lower_min, -1.0, 1e-6);
	CHECK_NEAR(c.camera_lower_max, 1.0, 1e-6);
}

static void test_world_size_sign_and_refresh_parity(void)
{
	float left72 = run_scale_rate(72.0, -1.0f);
	float left90 = run_scale_rate(90.0, -1.0f);
	float left120 = run_scale_rate(120.0, -1.0f);
	float right = run_scale_rate(90.0, 1.0f);
	CHECK_NEAR(left72, 104.0 * 1.5, 1e-3);
	CHECK_NEAR(left90, left72, 1e-4);
	CHECK_NEAR(left120, left72, 1e-3);
	CHECK_NEAR(right, 104.0 / 1.5, 1e-4);
}

static void test_deadzone_proportional_camera_sign_and_limits(void)
{
	sfvr_adjust_config c = config();
	c.max_dt_s = 1.0f; /* isolate stick-rate/sign behavior from the separate cap test */
	sfvr_adjust_state s;
	sfvr_adjust_init(&s);
	float scale = 104.0f, closer = 0.0f, lower = 0.0f;
	float stick[2] = { 0.2f, 1.0f };
	sfvr_adjust_update(&s, &c, stick, 1, 1, 0, 0, 0.0, &scale, &closer, &lower);
	stick[0] = 0.2f;
	stick[1] = 1.0f;
	sfvr_adjust_update(&s, &c, stick, 1, 1, 0, 0, 1.0, &scale, &closer, &lower);
	CHECK_NEAR(scale, 104.0, 1e-6); /* deadzone edge is neutral */
	CHECK_NEAR(closer, -0.25, 1e-6); /* up is farther */
	CHECK_NEAR(lower, -0.25, 1e-6); /* up is higher */

	stick[0] = 0.6f;
	stick[1] = -1.0f;
	sfvr_adjust_update(&s, &c, stick, 1, 1, 0, 0, 2.0, &scale, &closer, &lower);
	CHECK_NEAR(scale, 104.0 / sqrt(1.5), 1e-4); /* half travel after deadzone */
	CHECK_NEAR(closer, 0.0, 1e-6);
	CHECK_NEAR(lower, 0.0, 1e-6);

	closer = -10.0f;
	lower = -10.0f;
	stick[0] = 0.0f;
	stick[1] = 1.0f;
	sfvr_adjust_update(&s, &c, stick, 1, 1, 0, 0, 8.0, &scale, &closer, &lower);
	CHECK_NEAR(closer, -1.0, 1e-6);
	CHECK_NEAR(lower, -1.0, 1e-6);
	closer = 10.0f;
	lower = 10.0f;
	stick[1] = -1.0f;
	sfvr_adjust_update(&s, &c, stick, 1, 1, 0, 0, 9.0, &scale, &closer, &lower);
	CHECK_NEAR(closer, 0.8, 1e-6);
	CHECK_NEAR(lower, 1.0, 1e-6);
}

static void test_delta_cap_and_context_priority(void)
{
	sfvr_adjust_config c = config();
	sfvr_adjust_state s;
	sfvr_adjust_init(&s);
	float scale = 104.0f, closer = 0.0f, lower = 0.0f;
	float stick[2] = { -1.0f, 0.0f };
	sfvr_adjust_update(&s, &c, stick, 1, 1, 0, 0, 1.0, &scale, &closer, &lower);
	stick[0] = -1.0f;
	sfvr_adjust_update(&s, &c, stick, 1, 1, 0, 0, 5.0, &scale, &closer, &lower);
	CHECK_NEAR(scale, 104.0 * pow(1.5, 1.0 / 30.0), 1e-4);

	float before = scale;
	stick[0] = 1.0f;
	CHECK_INT(sfvr_adjust_update(&s, &c, stick, 1, 1, 1, 0, 5.1, &scale, &closer, &lower), 0);
	CHECK_NEAR(scale, before, 1e-6);
	CHECK_NEAR(stick[0], 0.0, 1e-6); /* Menu has priority, modifier is still consumed */
	stick[0] = 1.0f;
	CHECK_INT(sfvr_adjust_update(&s, &c, stick, 1, 1, 0, 1, 5.2, &scale, &closer, &lower), 0);
	CHECK_NEAR(stick[0], 0.0, 1e-6); /* View has priority too */
	stick[0] = 0.0f;
	sfvr_adjust_update(&s, &c, stick, 1, 0, 0, 0, 5.3, &scale, &closer, &lower);
	stick[0] = 0.0f;
	sfvr_adjust_update(&s, &c, stick, 0, 1, 0, 0, 5.4, &scale, &closer, &lower);
	CHECK_NEAR(stick[0], 0.0, 1e-6); /* first post-release neutral sample re-arms */
	stick[0] = 1.0f;
	sfvr_adjust_update(&s, &c, stick, 0, 1, 0, 0, 5.5, &scale, &closer, &lower);
	CHECK_NEAR(stick[0], 1.0, 1e-6); /* ordinary right-stick input resumes */
}

static void test_release_and_focus_loss_rearm(void)
{
	sfvr_adjust_config c = config();
	sfvr_adjust_state s;
	sfvr_adjust_init(&s);
	float scale = 104.0f, closer = 0.0f, lower = 0.0f;
	float stick[2] = { 0.8f, -0.6f };
	sfvr_adjust_update(&s, &c, stick, 1, 1, 0, 0, 0.0, &scale, &closer, &lower);
	CHECK_NEAR(stick[0], 0.0, 1e-6);
	stick[0] = 0.8f;
	stick[1] = -0.6f;
	sfvr_adjust_update(&s, &c, stick, 0, 1, 0, 0, 0.1, &scale, &closer, &lower);
	CHECK_NEAR(stick[0], 0.0, 1e-6);
	CHECK_NEAR(stick[1], 0.0, 1e-6);
	stick[0] = 0.1f;
	stick[1] = -0.1f;
	sfvr_adjust_update(&s, &c, stick, 0, 1, 0, 0, 0.2, &scale, &closer, &lower);
	CHECK_NEAR(stick[0], 0.1, 1e-6);
	stick[0] = 0.8f;
	sfvr_adjust_focus_lost(&s);
	CHECK_INT(s.display_time_valid, 0);
	stick[1] = 0.0f;
	sfvr_adjust_update(&s, &c, stick, 0, 1, 0, 0, 2.0, &scale, &closer, &lower);
	CHECK_NEAR(stick[0], 0.0, 1e-6); /* no stale turn on focus return */
	stick[0] = 0.0f;
	sfvr_adjust_update(&s, &c, stick, 0, 1, 0, 0, 2.1, &scale, &closer, &lower);
	stick[0] = 0.8f;
	sfvr_adjust_update(&s, &c, stick, 0, 1, 0, 0, 2.2, &scale, &closer, &lower);
	CHECK_NEAR(stick[0], 0.8, 1e-6); /* ordinary right-stick input resumes */
}


static void test_centered_release_still_requires_neutral_after_release(void)
{
	sfvr_adjust_config c = config();
	sfvr_adjust_state s;
	sfvr_adjust_init(&s);
	float scale = 104.0f, closer = 0.0f, lower = 0.0f;
	float stick[2] = { 0.0f, 0.0f };
	sfvr_adjust_update(&s, &c, stick, 1, 1, 0, 0, 1.0, &scale, &closer, &lower);
	stick[0] = stick[1] = 0.0f;
	sfvr_adjust_update(&s, &c, stick, 0, 1, 0, 0, 1.1, &scale, &closer, &lower);
	CHECK_INT(s.right_waiting_for_neutral, 1); /* release sample cannot re-arm itself */
	stick[0] = -1.0f;
	sfvr_adjust_update(&s, &c, stick, 1, 1, 0, 0, 1.2, &scale, &closer, &lower);
	CHECK_NEAR(scale, 104.0, 1e-6); /* re-gripping before a post-release neutral stays muted */
	CHECK_NEAR(stick[0], 0.0, 1e-6);
	stick[0] = 0.0f;
	sfvr_adjust_update(&s, &c, stick, 0, 1, 0, 0, 1.3, &scale, &closer, &lower);
	CHECK_INT(s.right_waiting_for_neutral, 1); /* a release edge still needs a later neutral sample */
	sfvr_adjust_update(&s, &c, stick, 0, 1, 0, 0, 1.4, &scale, &closer, &lower);
	CHECK_INT(s.right_waiting_for_neutral, 0);
	stick[0] = -1.0f;
	sfvr_adjust_update(&s, &c, stick, 0, 1, 0, 0, 1.5, &scale, &closer, &lower);
	CHECK_NEAR(stick[0], -1.0, 1e-6); /* ordinary right-stick gameplay resumes */
}

int main(void)
{
	RUN(test_default_config);
	RUN(test_world_size_sign_and_refresh_parity);
	RUN(test_deadzone_proportional_camera_sign_and_limits);
	RUN(test_delta_cap_and_context_priority);
	RUN(test_release_and_focus_loss_rearm);
	RUN(test_centered_release_still_requires_neutral_after_release);
	return SFVR_TEST_RESULT("test_adjust");
}
