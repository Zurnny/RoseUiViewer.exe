# embed_file(<input> <output.cpp> <symbol>) : writes a .cpp holding the file's bytes.
function(embed_file input output symbol)
	file(READ "${input}" hex HEX)
	string(LENGTH "${hex}" hex_len)
	math(EXPR size "${hex_len} / 2")
	string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
	string(REGEX REPLACE "(0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,)" "\\1\n" bytes "${bytes}")
	file(WRITE "${output}" "// Generated from ${input}\n#include <cstddef>\nextern const unsigned char ${symbol}[] = {\n${bytes}\n};\nextern const size_t ${symbol}_size = ${size};\n")
endfunction()
