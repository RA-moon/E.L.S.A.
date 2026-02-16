idf_component_register(
  SRCS
    "main.cpp"
    "animation_engine.cpp"
    "animation_manager.cpp"
    "frame_interpolation.cpp"
    "hair_strip.cpp"
    "led_utils.cpp"
    "audio_processor.cpp"
    "audio_scheduler.cpp"
    "runtime_config.cpp"
    "wave_position.cpp"
    "waveform.cpp"
    "animated_circles.cpp"
    "animated_circles_reversed.cpp"
    "animated_lines.cpp"
    "animated_lines_reversed.cpp"
    "led_strip_driver.c"
    "led_strip_encoder.c"
  INCLUDE_DIRS "."
  REQUIRES esp-dsp
)
