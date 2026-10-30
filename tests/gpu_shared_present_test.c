#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "video/gpu_present.c"

#define REQUIRE(condition, message) do { \
    if (!(condition)) { fprintf(stderr, "FAIL: %s\n", message); return 0; } \
} while (0)

static int publish(ID3D11DeviceContext *ctx, ID3D11Texture2D *texture,
                   IDXGIKeyedMutex *mutex, HANDLE handle, uint64_t frame_id,
                   const uint32_t *pixels)
{
    HRESULT hr = IDXGIKeyedMutex_AcquireSync(mutex, 0, 0);
    REQUIRE(hr == S_OK, "producer acquires shared frame");
    ID3D11DeviceContext_UpdateSubresource(ctx, (ID3D11Resource *)texture, 0, NULL,
                                         pixels, 8 * 4, 0);
    ID3D11DeviceContext_Flush(ctx);
    REQUIRE(SUCCEEDED(IDXGIKeyedMutex_ReleaseSync(mutex, 1)), "producer releases shared frame");
    REQUIRE(gpu_present_submit_shared(handle, 8, 8, frame_id), "submit shared frame");
    return 1;
}

static int wait_for_discard(IDXGIKeyedMutex *producer, int allow_shared, uint64_t frame_id)
{
    unsigned i;
    for (i = 0; i < 5000; ++i) {
        int held;
        HRESULT hr;
        REQUIRE(!take_shared(allow_shared, frame_id, &held) && !held,
                "fallback does not use a shared frame");
        hr = IDXGIKeyedMutex_AcquireSync(producer, 0, 0);
        if (hr == S_OK) {
            REQUIRE(SUCCEEDED(IDXGIKeyedMutex_ReleaseSync(producer, 0)),
                    "release discarded frame");
            return 1;
        }
        REQUIRE(hr == WAIT_TIMEOUT, "wait for discarded frame");
        Sleep(1);
    }
    fprintf(stderr, "FAIL: timed out waiting for shared-frame discard\n");
    return 0;
}

static int run_shared_test(int hardware)
{
    D3D_FEATURE_LEVEL feature = D3D_FEATURE_LEVEL_11_0, obtained;
    D3D_DRIVER_TYPE driver = hardware ? D3D_DRIVER_TYPE_HARDWARE : D3D_DRIVER_TYPE_WARP;
    ID3D11Device *producer = NULL;
    ID3D11DeviceContext *producer_ctx = NULL;
    ID3D11Texture2D *texture = NULL, *staging = NULL;
    IDXGIResource *resource = NULL;
    IDXGIKeyedMutex *mutex = NULL;
    D3D11_TEXTURE2D_DESC td;
    D3D11_MAPPED_SUBRESOURCE mapped;
    ID3D11ShaderResourceView *view = NULL;
    HANDLE handle = NULL;
    uint32_t pixels[64], x, y;
    unsigned i;
    int held = 0;
    HRESULT hr;

    REQUIRE(SUCCEEDED(D3D11CreateDevice(NULL, driver, NULL, 0, &feature, 1,
                                        D3D11_SDK_VERSION, &producer, &obtained, &producer_ctx)),
            "create producer device");
    REQUIRE(SUCCEEDED(D3D11CreateDevice(NULL, driver, NULL, 0, &feature, 1,
                                        D3D11_SDK_VERSION, &s_dev, &obtained, &s_ctx)),
            "create presenter device");
    memset(&td, 0, sizeof td);
    td.Width = td.Height = 8;
    td.MipLevels = td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    td.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
    REQUIRE(SUCCEEDED(ID3D11Device_CreateTexture2D(producer, &td, NULL, &texture)),
            "create shared texture");
    REQUIRE(SUCCEEDED(ID3D11Texture2D_QueryInterface(texture, &IID_IDXGIResource,
                                                      (void **)&resource)),
            "query shared resource");
    REQUIRE(SUCCEEDED(IDXGIResource_GetSharedHandle(resource, &handle)) && handle,
            "obtain shared handle");
    REQUIRE(SUCCEEDED(ID3D11Texture2D_QueryInterface(texture, &IID_IDXGIKeyedMutex,
                                                      (void **)&mutex)),
            "query producer keyed mutex");
    s_w = s_h = 8;
    InterlockedExchange(&s_shared_ready, 1);
    REQUIRE(gpu_present_shared_ready(8, 8), "shared presenter is ready");
    REQUIRE(!gpu_present_shared_ready(16, 8), "reject incompatible frame size");
    REQUIRE(_putenv_s("RECOMP_FB_VA", "1") == 0 &&
            !gpu_present_shared_ready(8, 8) &&
            _putenv_s("RECOMP_FB_VA", "") == 0, "pinned frames use CPU presentation");
    for (i = 0; i < 64; ++i)
        pixels[i] = 0xFF000000u | (i * 0x00010203u);
    REQUIRE(publish(producer_ctx, texture, mutex, handle, 42, pixels), "publish first frame");
    REQUIRE(!gpu_present_submit_shared(handle, 8, 8, 42), "bound the pending queue");
    REQUIRE(!take_shared(1, 41, &held) && !held, "do not show a frame before its CPU snapshot");
    REQUIRE(IDXGIKeyedMutex_AcquireSync(mutex, 0, 0) == WAIT_TIMEOUT,
            "future frame remains owned by the presenter");
    for (i = 0; i < 5000 && !held; ++i) {
        view = take_shared(1, 42, &held);
        if (!held)
            Sleep(1);
    }
    REQUIRE(view && held, "take matching shared frame");
    memset(&td, 0, sizeof td);
    td.Width = td.Height = 8;
    td.MipLevels = td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_STAGING;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    REQUIRE(SUCCEEDED(ID3D11Device_CreateTexture2D(s_dev, &td, NULL, &staging)),
            "create shared-frame verification texture");
    ID3D11DeviceContext_CopyResource(s_ctx, (ID3D11Resource *)staging,
                                     (ID3D11Resource *)s_shared_texture);
    hr = ID3D11DeviceContext_Map(s_ctx, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &mapped);
    REQUIRE(SUCCEEDED(hr), "map shared frame");
    for (y = 0; y < 8; ++y)
        for (x = 0; x < 8; ++x)
            REQUIRE(((const uint32_t *)((const uint8_t *)mapped.pData + y * mapped.RowPitch))[x]
                    == pixels[y * 8 + x], "shared frame pixels match the producer");
    ID3D11DeviceContext_Unmap(s_ctx, (ID3D11Resource *)staging, 0);
    REQUIRE(SUCCEEDED(IDXGIKeyedMutex_ReleaseSync(s_shared_mutex, 0)),
            "return the presented frame to the producer");

    REQUIRE(publish(producer_ctx, texture, mutex, handle, 43, pixels), "publish overlay frame");
    REQUIRE(wait_for_discard(mutex, 0, 42), "CPU overlay discards a pending shared frame");
    REQUIRE(publish(producer_ctx, texture, mutex, handle, 44, pixels), "publish stale frame");
    REQUIRE(wait_for_discard(mutex, 1, 45), "stale frame is not displayed");
    REQUIRE(!take_shared(1, 45, &held) && !held, "empty queue falls back to CPU pixels");

    ID3D11Texture2D_Release(staging);
    IDXGIKeyedMutex_Release(mutex);
    IDXGIResource_Release(resource);
    ID3D11Texture2D_Release(texture);
    ID3D11DeviceContext_Release(producer_ctx);
    ID3D11Device_Release(producer);
    gpu_present_shutdown();
    puts("shared presentation protocol passed");
    return 1;
}

int main(int argc, char **argv)
{
    if (argc != 2 || (strcmp(argv[1], "warp") && strcmp(argv[1], "hardware")))
        return 2;
    return run_shared_test(strcmp(argv[1], "hardware") == 0) ? 0 : 1;
}
