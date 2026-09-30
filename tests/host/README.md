# Apps Host Tests

These tests compile the production shared navigation helper, Settings factory
reset confirmation page, and Setup Wi-Fi adapter against small host fakes. They
cover RUN/BACK/OPEN_PAGE submission, gateway-owned ID copies, admission failure,
exactly-once completion, durable-reset request failure and retry, successful
request de-duplication, Wi-Fi session and operation filtering, callback delivery,
and retryable teardown.

Run the normal profile from the repository root:

```sh
cmake -S layers/apps/tests/host -B /tmp/mt-apps-normal -G Ninja \
    -DAPPS_SANITIZER=none
cmake --build /tmp/mt-apps-normal
ctest --test-dir /tmp/mt-apps-normal --output-on-failure

```

`APPS_SANITIZER=address` or `thread` is an additional profile for changes
involving memory ownership or concurrency. Use a separate build directory and
select the profile that matches the risk; both are not required by default.

These host checks do not replace ESP32-S3 UI, radio, or memory validation.
