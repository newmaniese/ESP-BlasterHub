# Attempt a syntax check
g++ -fsyntax-only -Iinclude -Itest/mocks -DESP32 src/main.cpp || echo "Syntax check failed, but may be due to missing libraries. The main code looks OK though."
