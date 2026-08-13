// Entry point only. The tests live beside this file, one translation unit per
// header under test: framing, variants, entity logic, receive buffer. They all
// link into the single binary platformio builds for this folder, which is the
// one the CI step runs by path.
#include <unity.h>

void test_profile_ids_are_unique_and_resolve();
void test_profile_table_matches_the_uuids_it_should_carry();
void test_every_profile_detects_from_its_own_pair();
void test_detect_is_case_insensitive();
void test_half_a_profile_is_not_a_match();
void test_right_chars_under_wrong_service_is_not_a_match();
void test_empty_scan_is_unknown();
void test_identify_dialect();
void test_byte_diversity();
void test_build_request_bulk_read();
void test_xor_covers_the_whole_frame();
void test_build_write();
void test_checksum_ok();
void test_checksum_length_boundary();
void test_extract_frames();
void test_length_byte_promising_more_than_arrives();
void test_vehicle_reply_decodes();
void test_decode_registers_edges();
void test_hex_dump();

void test_variant_from_name();
void test_variant_ordering_traps();
void test_variant_from_name_shape();

void test_scale_refused_until_the_vehicle_reports_a_factor();
void test_readback_inside_and_outside_the_declared_bounds();
void test_write_count_truncates_and_refuses_what_will_not_fit();
void test_apply_bit_leaves_the_neighbours_alone();
void test_bounded_and_signed_registers();

void test_accumulator_holds_a_frame_split_across_notifications();
void test_accumulator_keeps_the_tail_and_undoes_the_key();
void test_accumulator_drops_rather_than_overflows();

int main(int, char **) {
  UNITY_BEGIN();

  RUN_TEST(test_profile_ids_are_unique_and_resolve);
  RUN_TEST(test_profile_table_matches_the_uuids_it_should_carry);
  RUN_TEST(test_every_profile_detects_from_its_own_pair);
  RUN_TEST(test_detect_is_case_insensitive);
  RUN_TEST(test_half_a_profile_is_not_a_match);
  RUN_TEST(test_right_chars_under_wrong_service_is_not_a_match);
  RUN_TEST(test_empty_scan_is_unknown);
  RUN_TEST(test_identify_dialect);
  RUN_TEST(test_byte_diversity);
  RUN_TEST(test_build_request_bulk_read);
  RUN_TEST(test_xor_covers_the_whole_frame);
  RUN_TEST(test_build_write);
  RUN_TEST(test_checksum_ok);
  RUN_TEST(test_checksum_length_boundary);
  RUN_TEST(test_extract_frames);
  RUN_TEST(test_length_byte_promising_more_than_arrives);
  RUN_TEST(test_vehicle_reply_decodes);
  RUN_TEST(test_decode_registers_edges);
  RUN_TEST(test_hex_dump);

  RUN_TEST(test_variant_from_name);
  RUN_TEST(test_variant_ordering_traps);
  RUN_TEST(test_variant_from_name_shape);

  RUN_TEST(test_scale_refused_until_the_vehicle_reports_a_factor);
  RUN_TEST(test_readback_inside_and_outside_the_declared_bounds);
  RUN_TEST(test_write_count_truncates_and_refuses_what_will_not_fit);
  RUN_TEST(test_apply_bit_leaves_the_neighbours_alone);
  RUN_TEST(test_bounded_and_signed_registers);

  RUN_TEST(test_accumulator_holds_a_frame_split_across_notifications);
  RUN_TEST(test_accumulator_keeps_the_tail_and_undoes_the_key);
  RUN_TEST(test_accumulator_drops_rather_than_overflows);

  return UNITY_END();
}
