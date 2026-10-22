# =============================================================================
#  Abrasion Labs // OMNIS — Données embarquées (data/*.txt -> tableau d'octets)
#
#  Un tableau d'octets plutôt qu'une chaîne brute : MSVC limite les littéraux
#  de chaîne à ~16 Ko, et le lexique grandira.
# =============================================================================
function(omnis_embed_data out_file)
    set(content "// Fichier généré par cmake/OmnisEmbedData.cmake — ne pas éditer.\n")
    string(APPEND content "#include \"ai/EmbeddedData.h\"\n\nnamespace omnis::ai::data {\n\nnamespace {\n")
    set(accessors "")
    foreach(pair ${ARGN})
        string(REPLACE "=" ";" parts "${pair}")
        list(GET parts 0 name)
        list(GET parts 1 path)
        file(READ "${path}" hex HEX)
        string(LENGTH "${hex}" hex_length)
        math(EXPR size "${hex_length} / 2")
        string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
        string(APPEND content "const unsigned char k_${name}[] = {${bytes}0x00};\n")
        string(APPEND accessors "std::string_view ${name}() noexcept { return {reinterpret_cast<const char*>(k_${name}), ${size}}; }\n")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${path}")
    endforeach()
    string(APPEND content "\n} // namespace\n\n${accessors}\n} // namespace omnis::ai::data\n")
    file(WRITE "${out_file}" "${content}")
endfunction()
