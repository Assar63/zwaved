# src/cc-translator/CMakeLists.txt
# Bridge module that subscribes to bus ApplicationCommand events,
# runs the application/ codec decoders, and republishes typed bus
# events. The typed-decode body lives in CcTranslator.gen.cpp -- emitted
# by scripts/codegen/ from the manifest's `cc_translations:` block.
#
# Encapsulation.cpp is the hand-written companion: the inbound
# encapsulation-unwrap layer (CRC-16 #28; Transport Service #25 later)
# that verifies/reassembles a wrapped frame and republishes the inner CC
# as an ApplicationCommand, which the generated decoders then handle.

set_source_files_properties(
    "${ZWAVED_GENERATED_DIR}/CcTranslator.gen.cpp"
    PROPERTIES GENERATED TRUE
)

target_sources(zwaved PRIVATE
    "${ZWAVED_GENERATED_DIR}/CcTranslator.gen.cpp"
    Encapsulation.cpp
)

# Encapsulation.cpp includes an application/ codec header (Crc16Encap),
# whose generated constants live under generated/application/.
target_include_directories(zwaved PRIVATE "${ZWAVED_GENERATED_DIR}/application")
