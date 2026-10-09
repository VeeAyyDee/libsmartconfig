# SmartConfig parity addendum

Read01 for fast mode, connectedSTA/APSTA lifecycle and discovery;02 for protocol-specific omittedSSID recovery, public scanning and required tests. public-sdk contains separately licensed public declarations. fastmode-vectors.json contains original-only observations with no instruction bytes or implementation source. The specification author has not read fresh implementation src/ or idf/.

Original-only reproducer stays private at ../private/fastmode_oracle.py. It exercises original public/local fast setter and channel hopper with explicit IPC/driver/timer mocks. Run from workspace root with tools/python_env/idf6.0_py3.12_env/bin/python. Other behaviors are static original observations/publicSDK contracts, not yet an all-path composed oracle. See explicit unknowns in02.
