static void network_connected(X86Ctx*ctx){VARIANT_BOOL b=0;HRESULT hr=INetworkListManager_IsConnected((INetworkListManager*)self(ctx)->host,&b);if(SUCCEEDED(hr))st16(A(0),b);RET(hr);}
static void network_internet(X86Ctx*ctx){VARIANT_BOOL b=0;HRESULT hr=INetworkListManager_IsConnectedToInternet((INetworkListManager*)self(ctx)->host,&b);if(SUCCEEDED(hr))st16(A(0),b);RET(hr);}
static void network_connectivity(X86Ctx*ctx){RET(INetworkListManager_GetConnectivity((INetworkListManager*)self(ctx)->host,(NLM_CONNECTIVITY*)rt_ptr(A(0))));}

int oa_windows_network_create(X86Ctx *ctx)
{
    static const GUID network_manager_class = {
        0xdcb00c01, 0x570f, 0x4a9b, {0x8d, 0x69, 0x19, 0x9f, 0xdb, 0xa5, 0x72, 0x3b},
    };
    const GUID *class_id = rt_ptr(rt_arg(ctx, 0));
    const GUID *interface_id = rt_ptr(rt_arg(ctx, 3));
    if (!IsEqualGUID(class_id, &network_manager_class) || !IsEqualGUID(interface_id, &IID_INetworkListManager))
        return 0;

    void *out = NULL;
    HRESULT result = rt_arg(ctx, 1) ? CLASS_E_NOAGGREGATION :
        CoCreateInstance(class_id, NULL, rt_arg(ctx, 2), interface_id, &out);
    st32(rt_arg(ctx, 4), SUCCEEDED(result) ? wrap(out, NETMGR) : 0);
    RET(result);
    return 1;
}
