By submitting a contribution to this project, you agree to the terms outlined in the [Contributor License Agreement](CONTRIBUTOR_LICENSE_AGREEMENT.md).

## Tests

Run the tests before opening a pull request. CI runs them on macOS, Windows and Linux, and a failing test blocks the release.

**How:**

```bash
cmake --build build --target BoomerangTests -j8
ctest --test-dir build --output-on-failure
```

Add a test with your change: an audio test for the looper or processor, an editor test for the plugin window. See the Testing Guidelines in [`AGENTS.md`](AGENTS.md#testing-guidelines).
