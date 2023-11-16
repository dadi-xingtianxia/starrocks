// This file is licensed under the Elastic License 2.0. Copyright 2021-present, StarRocks Limited.

#include "exec/vectorized/json_scanner.h"

#include <gtest/gtest.h>

#include "column/datum_tuple.h"
#include "env/env_memory.h"
#include "gen_cpp/Descriptors_types.h"
#include "runtime/descriptor_helper.h"
#include "runtime/descriptors.h"
#include "runtime/runtime_state.h"

namespace starrocks::vectorized {

class JsonScannerTest : public ::testing::Test {
protected:
    std::unique_ptr<JsonScanner> create_json_scanner(const std::vector<TypeDescriptor>& types,
                                                     const std::vector<TBrokerRangeDesc>& ranges,
                                                     const std::vector<std::string>& col_names) {
        /// Init DescriptorTable
        TDescriptorTableBuilder desc_tbl_builder;
        TTupleDescriptorBuilder tuple_desc_builder;
        for (int i = 0; i < types.size(); ++i) {
            TSlotDescriptorBuilder slot_desc_builder;
            slot_desc_builder.type(types[i]).column_name(col_names[i]).length(types[i].len).nullable(true);
            tuple_desc_builder.add_slot(slot_desc_builder.build());
        }
        tuple_desc_builder.build(&desc_tbl_builder);

        DescriptorTbl* desc_tbl = nullptr;
        Status st = DescriptorTbl::create(&_pool, desc_tbl_builder.desc_tbl(), &desc_tbl, config::vector_chunk_size);
        CHECK(st.ok()) << st.to_string();

        /// Init RuntimeState
        _state->set_desc_tbl(desc_tbl);
        _state->init_instance_mem_tracker();

        /// TBrokerScanRangeParams
        TBrokerScanRangeParams* params = _pool.add(new TBrokerScanRangeParams());
        params->strict_mode = true;
        params->dest_tuple_id = 0;
        params->src_tuple_id = 0;
        for (int i = 0; i < types.size(); i++) {
            params->expr_of_dest_slot[i] = TExpr();
            params->expr_of_dest_slot[i].nodes.emplace_back(TExprNode());
            params->expr_of_dest_slot[i].nodes[0].__set_type(types[i].to_thrift());
            params->expr_of_dest_slot[i].nodes[0].__set_node_type(TExprNodeType::SLOT_REF);
            params->expr_of_dest_slot[i].nodes[0].__set_is_nullable(true);
            params->expr_of_dest_slot[i].nodes[0].__set_slot_ref(TSlotRef());
            params->expr_of_dest_slot[i].nodes[0].slot_ref.__set_slot_id(i);
            params->expr_of_dest_slot[i].nodes[0].__set_type(types[i].to_thrift());
        }

        for (int i = 0; i < types.size(); i++) {
            params->src_slot_ids.emplace_back(i);
        }

        TBrokerScanRange* broker_scan_range = _pool.add(new TBrokerScanRange());
        broker_scan_range->params = *params;
        broker_scan_range->ranges = ranges;
        return std::make_unique<JsonScanner>(_state, _profile, *broker_scan_range, _counter);
    }

    void SetUp() override {
        config::vector_chunk_size = 4096;
        _profile = _pool.add(new RuntimeProfile("test"));
        _counter = _pool.add(new ScannerCounter());
        _state = _pool.add(new RuntimeState(TQueryGlobals()));
        std::string starrocks_home = getenv("STARROCKS_HOME");
        _file_names = std::vector<std::string>{starrocks_home + "./be/test/exec/test_data/json_scanner/test1.json",
                                               starrocks_home + "./be/test/exec/test_data/json_scanner/test2.json",
                                               starrocks_home + "./be/test/exec/test_data/json_scanner/test3.json",
                                               starrocks_home + "./be/test/exec/test_data/json_scanner/test4.json",
                                               starrocks_home + "./be/test/exec/test_data/json_scanner/test8.json"};
    }

    void TearDown() override {}

private:
    RuntimeProfile* _profile = nullptr;
    ScannerCounter* _counter = nullptr;
    RuntimeState* _state = nullptr;
    ObjectPool _pool;
    std::vector<std::string> _file_names;
};

TEST_F(JsonScannerTest, test_json_without_path) {
    std::vector<TypeDescriptor> types;
    types.emplace_back(TypeDescriptor::create_varchar_type(20));
    types.emplace_back(TypeDescriptor::create_varchar_type(20));
    types.emplace_back(TypeDescriptor::create_varchar_type(20));
    types.emplace_back(TYPE_DOUBLE);

    std::vector<TBrokerRangeDesc> ranges;
    TBrokerRangeDesc range;
    range.format_type = TFileFormatType::FORMAT_JSON;
    range.strip_outer_array = true;
    range.__isset.strip_outer_array = true;
    range.__isset.jsonpaths = false;
    range.__isset.json_root = false;
    range.__set_path("./be/test/exec/test_data/json_scanner/test1.json");
    ranges.emplace_back(range);

    auto scanner = create_json_scanner(types, ranges, {"category", "author", "title", "price"});

    Status st;
    st = scanner->open();
    ASSERT_TRUE(st.ok());

    ChunkPtr chunk = scanner->get_next().value();
    EXPECT_EQ(4, chunk->num_columns());
    EXPECT_EQ(2, chunk->num_rows());

    EXPECT_EQ("['reference', 'NigelRees', 'SayingsoftheCentury', 8.95]", chunk->debug_row(0));
    EXPECT_EQ("['fiction', 'EvelynWaugh', 'SwordofHonour', 12.99]", chunk->debug_row(1));
}

TEST_F(JsonScannerTest, test_json_with_path) {
    std::vector<TypeDescriptor> types;
    types.emplace_back(TypeDescriptor::create_varchar_type(20));
    types.emplace_back(TypeDescriptor::create_varchar_type(20));
    types.emplace_back(TypeDescriptor::create_varchar_type(20));
    types.emplace_back(TYPE_INT);

    std::vector<TBrokerRangeDesc> ranges;
    TBrokerRangeDesc range;
    range.format_type = TFileFormatType::FORMAT_JSON;
    range.strip_outer_array = true;
    range.__isset.strip_outer_array = true;
    range.__isset.jsonpaths = true;
    range.jsonpaths = "[\"$.k1\", \"$.kind\", \"$.keyname.ip\", \"$.keyname.value\"]";
    range.__isset.json_root = false;
    range.__set_path("./be/test/exec/test_data/json_scanner/test2.json");
    ranges.emplace_back(range);

    auto scanner = create_json_scanner(types, ranges, {"k1", "kind", "ip", "value"});

    Status st;
    st = scanner->open();
    ASSERT_TRUE(st.ok());

    ChunkPtr chunk = scanner->get_next().value();
    EXPECT_EQ(4, chunk->num_columns());
    EXPECT_EQ(2, chunk->num_rows());

    EXPECT_EQ("['v1', 'server', '10.10.0.1', 20]", chunk->debug_row(0));
    EXPECT_EQ("['v2', 'server', '10.20.1.1', 20]", chunk->debug_row(1));
}

TEST_F(JsonScannerTest, test_one_level_array) {
    std::vector<TypeDescriptor> types;
    TypeDescriptor t1(TYPE_ARRAY);
    t1.children.emplace_back(TypeDescriptor::create_varchar_type(20));
    types.emplace_back(t1);

    TypeDescriptor t2(TYPE_ARRAY);
    t2.children.emplace_back(TYPE_INT);
    types.emplace_back(t2);

    std::vector<TBrokerRangeDesc> ranges;
    TBrokerRangeDesc range;
    range.format_type = TFileFormatType::FORMAT_JSON;
    range.strip_outer_array = true;
    range.__isset.strip_outer_array = true;
    range.__isset.jsonpaths = true;
    range.jsonpaths = "[\"$.keyname.ip\", \"$.keyname.value\"]";
    range.__isset.json_root = false;
    range.__set_path("./be/test/exec/test_data/json_scanner/test3.json");
    ranges.emplace_back(range);

    auto scanner = create_json_scanner(types, ranges, {"k1", "kind", "ip", "value"});

    Status st;
    st = scanner->open();
    ASSERT_TRUE(st.ok());

    ChunkPtr chunk = scanner->get_next().value();
    EXPECT_EQ(2, chunk->num_columns());
    EXPECT_EQ(1, chunk->num_rows());

    EXPECT_EQ("[['10.10.0.1', '10.20.1.1'], [10, 20]]", chunk->debug_row(0));
}

TEST_F(JsonScannerTest, test_two_level_array) {
    std::vector<TypeDescriptor> types;
    TypeDescriptor t1(TYPE_ARRAY);
    t1.children.emplace_back(TYPE_ARRAY);
    t1.children.back().children.emplace_back(TYPE_BIGINT);
    types.emplace_back(t1);

    std::vector<TBrokerRangeDesc> ranges;
    TBrokerRangeDesc range;
    range.format_type = TFileFormatType::FORMAT_JSON;
    range.strip_outer_array = true;
    range.__isset.strip_outer_array = true;
    range.__isset.jsonpaths = false;
    range.__isset.json_root = false;
    range.__set_path("./be/test/exec/test_data/json_scanner/test4.json");
    ranges.emplace_back(range);

    auto scanner = create_json_scanner(types, ranges, {"value"});

    Status st;
    st = scanner->open();
    ASSERT_TRUE(st.ok());

    ChunkPtr chunk = scanner->get_next().value();
    EXPECT_EQ(1, chunk->num_columns());
    EXPECT_EQ(1, chunk->num_rows());

    EXPECT_EQ("[[[10, 20], [30, 40]]]", chunk->debug_row(0));
}

TEST_F(JsonScannerTest, test_invalid_column_in_array) {
    std::vector<TypeDescriptor> types;
    TypeDescriptor t1(TYPE_ARRAY);
    t1.children.emplace_back(TYPE_ARRAY);
    t1.children.back().children.emplace_back(TYPE_SMALLINT);
    types.emplace_back(t1);

    std::vector<TBrokerRangeDesc> ranges;
    TBrokerRangeDesc range;
    range.format_type = TFileFormatType::FORMAT_JSON;
    range.strip_outer_array = true;
    range.__isset.strip_outer_array = true;
    range.__isset.jsonpaths = false;
    range.__isset.json_root = false;
    range.__set_path("./be/test/exec/test_data/json_scanner/test5.json");
    ranges.emplace_back(range);

    auto scanner = create_json_scanner(types, ranges, {"value"});

    Status st;
    st = scanner->open();
    ASSERT_TRUE(st.ok());

    ChunkPtr chunk = scanner->get_next().value();
    EXPECT_EQ(1, chunk->num_columns());
    EXPECT_EQ(1, chunk->num_rows());

    EXPECT_EQ("[NULL]", chunk->debug_row(0));
}

TEST_F(JsonScannerTest, test_invalid_nested_level1) {
    // the nested level in schema is larger than json
    std::vector<TypeDescriptor> types;
    TypeDescriptor t1(TYPE_ARRAY);
    t1.children.emplace_back(TYPE_ARRAY);
    t1.children.back().children.emplace_back(TYPE_TINYINT);
    types.emplace_back(t1);

    std::vector<TBrokerRangeDesc> ranges;
    TBrokerRangeDesc range;
    range.format_type = TFileFormatType::FORMAT_JSON;
    range.strip_outer_array = true;
    range.__isset.strip_outer_array = true;
    range.__isset.jsonpaths = false;
    range.__isset.json_root = false;
    range.__set_path("./be/test/exec/test_data/json_scanner/test6.json");
    ranges.emplace_back(range);

    auto scanner = create_json_scanner(types, ranges, {"value"});

    Status st;
    st = scanner->open();
    ASSERT_TRUE(st.ok());

    ChunkPtr chunk = scanner->get_next().value();

    EXPECT_EQ(1, chunk->num_columns());
    EXPECT_EQ(1, chunk->num_rows());

    EXPECT_EQ("[NULL]", chunk->debug_row(0));
}

TEST_F(JsonScannerTest, test_invalid_nested_level2) {
    // the nested level in schema is less than json
    std::vector<TypeDescriptor> types;
    TypeDescriptor t1(TYPE_ARRAY);
    t1.children.emplace_back(TYPE_LARGEINT);
    types.emplace_back(t1);

    std::vector<TBrokerRangeDesc> ranges;
    TBrokerRangeDesc range;
    range.format_type = TFileFormatType::FORMAT_JSON;
    range.strip_outer_array = true;
    range.__isset.strip_outer_array = true;
    range.__isset.jsonpaths = false;
    range.__isset.json_root = false;
    range.__set_path("./be/test/exec/test_data/json_scanner/test7.json");
    ranges.emplace_back(range);

    auto scanner = create_json_scanner(types, ranges, {"value"});

    Status st;
    st = scanner->open();
    ASSERT_TRUE(st.ok());

    ChunkPtr chunk = scanner->get_next().value();
    EXPECT_EQ(1, chunk->num_columns());
    EXPECT_EQ(1, chunk->num_rows());

    EXPECT_EQ("[[NULL, NULL]]", chunk->debug_row(0));
}

TEST_F(JsonScannerTest, test_json_with_long_string) {
    std::vector<TypeDescriptor> types;
    types.emplace_back(TypeDescriptor::create_varchar_type(100));
    types.emplace_back(TypeDescriptor::create_varchar_type(100));

    std::vector<TBrokerRangeDesc> ranges;
    TBrokerRangeDesc range;
    range.format_type = TFileFormatType::FORMAT_JSON;
    range.strip_outer_array = true;
    range.__isset.strip_outer_array = true;
    range.__isset.jsonpaths = false;
    range.__isset.json_root = false;
    range.__set_path("./be/test/exec/test_data/json_scanner/test8.json");
    ranges.emplace_back(range);

    auto scanner = create_json_scanner(types, ranges, {"request", "ids"});

    Status st;
    st = scanner->open();
    ASSERT_TRUE(st.ok());

    ChunkPtr chunk = scanner->get_next().value();
    EXPECT_EQ(2, chunk->num_columns());
    EXPECT_EQ(1, chunk->num_rows());

    EXPECT_EQ("['{\"area\":\"beijing\",\"country\":\"china\"}', '[\"478472290\",\"478473274\"]']", chunk->debug_row(0));
}

TEST_F(JsonScannerTest, test_ndjson) {
    std::vector<TypeDescriptor> types;
    types.emplace_back(TypeDescriptor::create_varchar_type(20));
    types.emplace_back(TypeDescriptor::create_varchar_type(20));
    types.emplace_back(TypeDescriptor::create_varchar_type(20));
    types.emplace_back(TYPE_INT);

    std::vector<TBrokerRangeDesc> ranges;
    TBrokerRangeDesc range;
    range.format_type = TFileFormatType::FORMAT_JSON;
    range.strip_outer_array = false;
    range.__isset.strip_outer_array = false;
    range.__isset.jsonpaths = false;
    range.__isset.json_root = false;
    range.__set_path("./be/test/exec/test_data/json_scanner/test_ndjson.json");
    ranges.emplace_back(range);

    auto scanner = create_json_scanner(types, ranges, {"k1", "kind", "ip", "value"});

    Status st;
    st = scanner->open();
    ASSERT_TRUE(st.ok());

    ChunkPtr chunk = scanner->get_next().value();
    EXPECT_EQ(4, chunk->num_columns());
    EXPECT_EQ(5, chunk->num_rows());

    EXPECT_EQ("['v1', 'server', NULL, NULL]", chunk->debug_row(0));
    EXPECT_EQ("['v2', 'server', NULL, NULL]", chunk->debug_row(1));
    EXPECT_EQ("['v3', 'server', NULL, NULL]", chunk->debug_row(2));
    EXPECT_EQ("['v4', 'server', NULL, NULL]", chunk->debug_row(3));
    EXPECT_EQ("['v5', 'server', NULL, NULL]", chunk->debug_row(4));
}

TEST_F(JsonScannerTest, test_ndjson_with_jsonpath) {
    std::vector<TypeDescriptor> types;
    types.emplace_back(TypeDescriptor::create_varchar_type(20));
    types.emplace_back(TypeDescriptor::create_varchar_type(20));
    types.emplace_back(TypeDescriptor::create_varchar_type(20));
    types.emplace_back(TYPE_INT);

    std::vector<TBrokerRangeDesc> ranges;
    TBrokerRangeDesc range;
    range.format_type = TFileFormatType::FORMAT_JSON;
    range.strip_outer_array = false;
    range.__isset.strip_outer_array = false;
    range.__isset.jsonpaths = true;
    range.jsonpaths = "[\"$.k1\", \"$.kind\", \"$.keyname.ip\", \"$.keyname.value\"]";
    range.__isset.json_root = false;
    range.__set_path("./be/test/exec/test_data/json_scanner/test_ndjson.json");
    ranges.emplace_back(range);

    auto scanner = create_json_scanner(types, ranges, {"k1", "kind", "ip", "value"});

    Status st;
    st = scanner->open();
    ASSERT_TRUE(st.ok());

    ChunkPtr chunk = scanner->get_next().value();
    EXPECT_EQ(4, chunk->num_columns());
    EXPECT_EQ(5, chunk->num_rows());

    EXPECT_EQ("['v1', 'server', '10.10.0.1', 10]", chunk->debug_row(0));
    EXPECT_EQ("['v2', 'server', '10.10.0.2', 20]", chunk->debug_row(1));
    EXPECT_EQ("['v3', 'server', '10.10.0.3', 30]", chunk->debug_row(2));
    EXPECT_EQ("['v4', 'server', '10.10.0.4', 40]", chunk->debug_row(3));
    EXPECT_EQ("['v5', 'server', '10.10.0.5', 50]", chunk->debug_row(4));
}

TEST_F(JsonScannerTest, test_multi_type) {
    std::vector<TypeDescriptor> types;
    types.emplace_back(TYPE_BOOLEAN);

    types.emplace_back(TYPE_TINYINT);
    types.emplace_back(TYPE_SMALLINT);
    types.emplace_back(TYPE_INT);
    types.emplace_back(TYPE_BIGINT);
    // Numbers beyond range of uint64_t is not supported by json scanner.
    // Hence, we skip the test of LARGEINT.

    types.emplace_back(TYPE_FLOAT);
    types.emplace_back(TYPE_DOUBLE);

    types.emplace_back(TypeDescriptor::create_varchar_type(20));
    types.emplace_back(TYPE_DATE);
    types.emplace_back(TYPE_DATETIME);
    types.emplace_back(TypeDescriptor::create_varchar_type(20));

    types.emplace_back(TypeDescriptor::create_decimalv3_type(TYPE_DECIMAL128, 27, 9));
    types.emplace_back(TypeDescriptor::create_char_type(20));
    types.emplace_back(TYPE_TIME);

    std::vector<TBrokerRangeDesc> ranges;
    TBrokerRangeDesc range;
    range.format_type = TFileFormatType::FORMAT_JSON;
    range.strip_outer_array = false;
    range.__isset.strip_outer_array = false;
    range.__isset.jsonpaths = false;
    range.__isset.json_root = false;
    range.__set_path("./be/test/exec/test_data/json_scanner/test_multi_type.json");
    ranges.emplace_back(range);

    auto scanner =
            create_json_scanner(types, ranges,
                                {"f_bool", "f_tinyint", "f_smallint", "f_int", "f_bigint", "f_float", "f_double",
                                 "f_varchar", "f_date", "f_datetime", "f_array", "f_decimal", "f_char", "f_time"});

    Status st;
    st = scanner->open();
    ASSERT_TRUE(st.ok());

    ChunkPtr chunk = scanner->get_next().value();
    EXPECT_EQ(14, chunk->num_columns());
    EXPECT_EQ(1, chunk->num_rows());

    auto expected =
            "[1, 127, 32767, 2147483647, 9223372036854775807, 3.14, 3.14, 'starrocks', 2021-12-09, 2021-12-09 "
            "10:00:00, '[1,3,5]', 1234565789012345678901234567.123456789, 'starrocks', 36000]";

    EXPECT_EQ(expected, chunk->debug_row(0));
}

TEST_F(JsonScannerTest, test_cast_type) {
    std::vector<TypeDescriptor> types;
    types.emplace_back(TypeDescriptor::create_varchar_type(20));
    types.emplace_back(TypeDescriptor::create_varchar_type(20));
    types.emplace_back(TypeDescriptor::create_varchar_type(20));
    types.emplace_back(TYPE_DOUBLE);
    types.emplace_back(TYPE_INT);

    std::vector<TBrokerRangeDesc> ranges;
    TBrokerRangeDesc range;
    range.format_type = TFileFormatType::FORMAT_JSON;
    range.strip_outer_array = false;
    range.__isset.strip_outer_array = false;
    range.__isset.jsonpaths = false;
    range.__isset.json_root = false;
    range.__set_path("./be/test/exec/test_data/json_scanner/test_cast_type.json");
    ranges.emplace_back(range);

    auto scanner =
            create_json_scanner(types, ranges, {"f_float", "f_bool", "f_int", "f_float_in_string", "f_int_in_string"});

    Status st;
    st = scanner->open();
    ASSERT_TRUE(st.ok());

    ChunkPtr chunk = scanner->get_next().value();
    EXPECT_EQ(5, chunk->num_columns());
    EXPECT_EQ(1, chunk->num_rows());

    EXPECT_EQ("['3.14', '1', '123', 3.14, 123]", chunk->debug_row(0));
}

TEST_F(JsonScannerTest, test_expanded_with_json_root) {
    std::vector<TypeDescriptor> types;
    types.emplace_back(TypeDescriptor::create_varchar_type(20));
    types.emplace_back(TypeDescriptor::create_varchar_type(20));
    types.emplace_back(TypeDescriptor::create_varchar_type(20));

    std::vector<TBrokerRangeDesc> ranges;
    TBrokerRangeDesc range;
    range.format_type = TFileFormatType::FORMAT_JSON;
    range.strip_outer_array = true;
    range.jsonpaths = "";
    range.json_root = "$.data";

    range.__isset.strip_outer_array = true;
    range.__isset.jsonpaths = false;
    range.__isset.json_root = true;

    range.__set_path("./be/test/exec/test_data/json_scanner/test_expanded_array.json");
    ranges.emplace_back(range);

    auto scanner = create_json_scanner(types, ranges, {"k1", "kind", "keyname"});

    Status st;
    st = scanner->open();
    ASSERT_TRUE(st.ok());

    ChunkPtr chunk = scanner->get_next().value();
    EXPECT_EQ(3, chunk->num_columns());
    EXPECT_EQ(5, chunk->num_rows());

    EXPECT_EQ("['v1', 'server', NULL]", chunk->debug_row(0));
    EXPECT_EQ("['v2', 'server', NULL]", chunk->debug_row(1));
    EXPECT_EQ("['v3', 'server', NULL]", chunk->debug_row(2));
    EXPECT_EQ("['v4', 'server', NULL]", chunk->debug_row(3));
    EXPECT_EQ("['v5', 'server', NULL]", chunk->debug_row(4));
}

TEST_F(JsonScannerTest, test_ndjson_expanded_with_json_root) {
    std::vector<TypeDescriptor> types;
    types.emplace_back(TypeDescriptor::create_varchar_type(20));
    types.emplace_back(TypeDescriptor::create_varchar_type(20));
    types.emplace_back(TypeDescriptor::create_varchar_type(20));

    std::vector<TBrokerRangeDesc> ranges;
    TBrokerRangeDesc range;
    range.format_type = TFileFormatType::FORMAT_JSON;
    range.strip_outer_array = true;
    range.jsonpaths = "";
    range.json_root = "$.data";

    range.__isset.strip_outer_array = true;
    range.__isset.jsonpaths = false;
    range.__isset.json_root = true;

    range.__set_path("./be/test/exec/test_data/json_scanner/test_ndjson_expanded_array.json");
    ranges.emplace_back(range);

    auto scanner = create_json_scanner(types, ranges, {"k1", "kind", "keyname"});

    Status st;
    st = scanner->open();
    ASSERT_TRUE(st.ok());

    ChunkPtr chunk = scanner->get_next().value();
    EXPECT_EQ(3, chunk->num_columns());
    EXPECT_EQ(5, chunk->num_rows());

    EXPECT_EQ("['v1', 'server', NULL]", chunk->debug_row(0));
    EXPECT_EQ("['v2', 'server', NULL]", chunk->debug_row(1));
    EXPECT_EQ("['v3', 'server', NULL]", chunk->debug_row(2));
    EXPECT_EQ("['v4', 'server', NULL]", chunk->debug_row(3));
    EXPECT_EQ("['v5', 'server', NULL]", chunk->debug_row(4));
}

// this test covers json_scanner.cpp:_construct_row_in_object_order.
TEST_F(JsonScannerTest, test_construct_row_in_object_order) {
    std::vector<TypeDescriptor> types;
    types.emplace_back(TypeDescriptor::create_varchar_type(20));
    types.emplace_back(TypeDescriptor::create_varchar_type(20));
    types.emplace_back(TypeDescriptor::create_varchar_type(20));
    types.emplace_back(TypeDescriptor::create_varchar_type(20));
    types.emplace_back(TypeDescriptor::create_varchar_type(20));
    types.emplace_back(TypeDescriptor::create_varchar_type(20));
    types.emplace_back(TypeDescriptor::create_varchar_type(20));
    types.emplace_back(TYPE_DOUBLE);
    types.emplace_back(TYPE_DOUBLE);
    types.emplace_back(TYPE_INT);
    types.emplace_back(TYPE_INT);

    std::vector<TBrokerRangeDesc> ranges;
    TBrokerRangeDesc range;
    range.format_type = TFileFormatType::FORMAT_JSON;
    range.strip_outer_array = false;
    range.__isset.strip_outer_array = false;
    range.__isset.jsonpaths = false;
    range.__isset.json_root = false;
    range.__set_path("./be/test/exec/test_data/json_scanner/test_cast_type.json");
    ranges.emplace_back(range);

    auto scanner = create_json_scanner(types, ranges,
                                       {"f_dummy_0", "f_float", "f_dummy_1", "f_bool", "f_dummy_2", "f_int",
                                        "f_dummy_3", "f_float_in_string", "f_dummy_4", "f_int_in_string", "f_dummy_5"});

    Status st;
    st = scanner->open();
    ASSERT_TRUE(st.ok());

    ChunkPtr chunk = scanner->get_next().value();
    EXPECT_EQ(11, chunk->num_columns());
    EXPECT_EQ(1, chunk->num_rows());

    EXPECT_EQ("[NULL, '3.14', NULL, '1', NULL, '123', NULL, 3.14, NULL, 123, NULL]", chunk->debug_row(0));
}

TEST_F(JsonScannerTest, test_jsonroot_with_jsonpath) {
    std::vector<TypeDescriptor> types;
    types.emplace_back(TypeDescriptor::create_varchar_type(20));
    types.emplace_back(TypeDescriptor::create_varchar_type(20));

    std::vector<TBrokerRangeDesc> ranges;
    TBrokerRangeDesc range;
    range.format_type = TFileFormatType::FORMAT_JSON;
    range.strip_outer_array = false;
    range.__isset.strip_outer_array = false;
    range.__isset.jsonpaths = true;
    range.__isset.json_root = true;

    range.jsonpaths = "[\"$.ip\", \"$.value\"]";
    range.json_root = "$.keyname";

    range.__set_path("./be/test/exec/test_data/json_scanner/test_ndjson.json");
    ranges.emplace_back(range);

    auto scanner = create_json_scanner(types, ranges, {"ip", "value"});

    Status st;
    st = scanner->open();
    ASSERT_TRUE(st.ok());

    ChunkPtr chunk = scanner->get_next().value();
    EXPECT_EQ(2, chunk->num_columns());
    EXPECT_EQ(5, chunk->num_rows());

    EXPECT_EQ("['10.10.0.1', '10']", chunk->debug_row(0));
    EXPECT_EQ("['10.10.0.2', '20']", chunk->debug_row(1));
    EXPECT_EQ("['10.10.0.3', '30']", chunk->debug_row(2));
    EXPECT_EQ("['10.10.0.4', '40']", chunk->debug_row(3));
    EXPECT_EQ("['10.10.0.5', '50']", chunk->debug_row(4));
}

TEST_F(JsonScannerTest, test_expanded_with_jsonroot_and_extracted_by_jsonpath) {
    std::vector<TypeDescriptor> types;
    types.emplace_back(TypeDescriptor::create_varchar_type(20));
    types.emplace_back(TypeDescriptor::create_varchar_type(20));

    std::vector<TBrokerRangeDesc> ranges;
    TBrokerRangeDesc range;
    range.format_type = TFileFormatType::FORMAT_JSON;
    range.strip_outer_array = true;
    range.jsonpaths = "[\"$.keyname.ip\", \"$.keyname.value\"]";
    range.json_root = "$.data";

    range.__isset.strip_outer_array = true;
    range.__isset.jsonpaths = true;
    range.__isset.json_root = true;

    range.__set_path("./be/test/exec/test_data/json_scanner/test_expanded_array.json");
    ranges.emplace_back(range);

    auto scanner = create_json_scanner(types, ranges, {"ip", "value"});

    Status st;
    st = scanner->open();
    ASSERT_TRUE(st.ok());

    ChunkPtr chunk = scanner->get_next().value();
    EXPECT_EQ(2, chunk->num_columns());
    EXPECT_EQ(5, chunk->num_rows());

    EXPECT_EQ("['10.10.0.1', '10']", chunk->debug_row(0));
    EXPECT_EQ("['10.10.0.2', '20']", chunk->debug_row(1));
    EXPECT_EQ("['10.10.0.3', '30']", chunk->debug_row(2));
    EXPECT_EQ("['10.10.0.4', '40']", chunk->debug_row(3));
    EXPECT_EQ("['10.10.0.5', '50']", chunk->debug_row(4));
}

TEST_F(JsonScannerTest, test_ndjson_expaned_with_jsonroot_and_extracted_by_jsonpath) {
    std::vector<TypeDescriptor> types;
    types.emplace_back(TypeDescriptor::create_varchar_type(20));
    types.emplace_back(TypeDescriptor::create_varchar_type(20));

    std::vector<TBrokerRangeDesc> ranges;
    TBrokerRangeDesc range;
    range.format_type = TFileFormatType::FORMAT_JSON;
    range.strip_outer_array = true;
    range.jsonpaths = "[\"$.keyname.ip\", \"$.keyname.value\"]";
    range.json_root = "$.data";

    range.__isset.strip_outer_array = true;
    range.__isset.jsonpaths = true;
    range.__isset.json_root = true;

    range.__set_path("./be/test/exec/test_data/json_scanner/test_ndjson_expanded_array.json");
    ranges.emplace_back(range);

    auto scanner = create_json_scanner(types, ranges, {"ip", "value"});

    Status st;
    st = scanner->open();
    ASSERT_TRUE(st.ok());

    ChunkPtr chunk = scanner->get_next().value();
    EXPECT_EQ(2, chunk->num_columns());
    EXPECT_EQ(5, chunk->num_rows());

    EXPECT_EQ("['10.10.0.1', '10']", chunk->debug_row(0));
    EXPECT_EQ("['10.10.0.2', '20']", chunk->debug_row(1));
    EXPECT_EQ("['10.10.0.3', '30']", chunk->debug_row(2));
    EXPECT_EQ("['10.10.0.4', '40']", chunk->debug_row(3));
    EXPECT_EQ("['10.10.0.5', '50']", chunk->debug_row(4));
}

} // namespace starrocks::vectorized