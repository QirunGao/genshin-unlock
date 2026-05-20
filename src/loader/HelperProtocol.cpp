#include "loader/HelperProtocol.hpp"

#include <glaze/glaze.hpp>
#include <glaze/glaze_exceptions.hpp>

#include <cstdint>
#include <vector>

namespace {
constexpr glz::opts opts {
    .null_terminated = false,
    .comments = false,
    .error_on_unknown_keys = true,
    .skip_null_members = false,
    .prettify = true,
    .minified = false,
    .indentation_char = ' ',
    .indentation_width = 4,
    .new_lines_in_arrays = true,
    .append_arrays = false,
    .error_on_missing_keys = true,
    .error_on_const_read = true,
    .bools_as_numbers = false,
    .quoted_num = false,
    .number = false,
    .raw = false,
    .raw_string = false,
    .structs_as_arrays = false,
    .partial_read = false
};
} // namespace

namespace z3lx::loader {
bool HelperRequest::IsValid(const uint64_t expectedNonce) const {
    return magic == helperProtocolMagic &&
        version == helperProtocolVersion &&
        type == helperRequestTypeLaunch &&
        nonce == expectedNonce;
}

void HelperRequest::Serialize(std::vector<uint8_t>& buffer) const {
    glz::ex::write<opts>(*this, buffer);
}

void HelperRequest::Deserialize(const std::vector<uint8_t>& buffer) {
    glz::ex::read<opts>(*this, buffer);
}
} // namespace z3lx::loader
