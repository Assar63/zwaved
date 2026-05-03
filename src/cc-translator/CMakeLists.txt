# src/cc-translator/CMakeLists.txt
# Bridge module that subscribes to bus ApplicationCommand events,
# runs the application/ codec decoders, and republishes typed bus
# events. The body lives entirely in CcTranslator.gen.cpp -- emitted
# by scripts/codegen/ from the manifest's `cc_translations:` block.
# This directory holds only the build glue, not source code.

set_source_files_properties(
    "${ZWAVED_GENERATED_DIR}/CcTranslator.gen.cpp"
    PROPERTIES GENERATED TRUE
)

target_sources(zwaved PRIVATE
    "${ZWAVED_GENERATED_DIR}/CcTranslator.gen.cpp"
)
