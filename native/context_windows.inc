/* Pointer-free descriptors pass unchanged; all object arrays become native
 * interface pointers. Stage functions share SDK-defined signatures. */
static ID3D11DeviceContext*dc(X86Ctx*ctx){return (ID3D11DeviceContext*)self(ctx)->host;}
static void context_clear(X86Ctx*ctx){ID3D11DeviceContext_ClearState(dc(ctx));}
static void context_flush(X86Ctx*ctx){ID3D11DeviceContext_Flush(dc(ctx));}
#define STAGE_FUNCS(S) \
static void S##_buffers(X86Ctx*ctx){unsigned n=A(1);if(n>14)rt_fatal("constant buffer count");ID3D11Buffer*v[14];for(unsigned i=0;i<n;i++)v[i]=(ID3D11Buffer*)host_object(ld32(A(2)+4*i));ID3D11DeviceContext_##S##SetConstantBuffers(dc(ctx),A(0),n,v);} \
static void S##_resources(X86Ctx*ctx){unsigned n=A(1);if(n>128)rt_fatal("shader resource count");ID3D11ShaderResourceView*v[128];for(unsigned i=0;i<n;i++)v[i]=(ID3D11ShaderResourceView*)host_object(ld32(A(2)+4*i));ID3D11DeviceContext_##S##SetShaderResources(dc(ctx),A(0),n,v);} \
static void S##_samplers(X86Ctx*ctx){unsigned n=A(1);if(n>16)rt_fatal("sampler count");ID3D11SamplerState*v[16];for(unsigned i=0;i<n;i++)v[i]=(ID3D11SamplerState*)host_object(ld32(A(2)+4*i));ID3D11DeviceContext_##S##SetSamplers(dc(ctx),A(0),n,v);}
STAGE_FUNCS(VS) STAGE_FUNCS(PS) STAGE_FUNCS(GS) STAGE_FUNCS(HS) STAGE_FUNCS(DS) STAGE_FUNCS(CS)
#define SHADER_BIND(S,T) static void S##_shader(X86Ctx*ctx){if(A(2))rt_fatal("dynamic shader class instances unqualified");ID3D11DeviceContext_##S##SetShader(dc(ctx),(T*)host_object(A(0)),NULL,0);}
SHADER_BIND(VS,ID3D11VertexShader) SHADER_BIND(PS,ID3D11PixelShader) SHADER_BIND(GS,ID3D11GeometryShader)
SHADER_BIND(HS,ID3D11HullShader) SHADER_BIND(DS,ID3D11DomainShader) SHADER_BIND(CS,ID3D11ComputeShader)
static void input_layout(X86Ctx*ctx){ID3D11DeviceContext_IASetInputLayout(dc(ctx),(ID3D11InputLayout*)host_object(A(0)));}
static void topology(X86Ctx*ctx){ID3D11DeviceContext_IASetPrimitiveTopology(dc(ctx),A(0));}
static void vertex_buffers(X86Ctx*ctx){unsigned n=A(1);if(n>32)rt_fatal("vertex buffer count");ID3D11Buffer*b[32];for(unsigned i=0;i<n;i++)b[i]=(ID3D11Buffer*)host_object(ld32(A(2)+4*i));ID3D11DeviceContext_IASetVertexBuffers(dc(ctx),A(0),n,b,(const UINT*)rt_ptr(A(3)),(const UINT*)rt_ptr(A(4)));}
static void index_buffer(X86Ctx*ctx){ID3D11DeviceContext_IASetIndexBuffer(dc(ctx),(ID3D11Buffer*)host_object(A(0)),A(1),A(2));}
static void raster_state(X86Ctx*ctx){ID3D11DeviceContext_RSSetState(dc(ctx),(ID3D11RasterizerState*)host_object(A(0)));}
static void viewports(X86Ctx*ctx){ID3D11DeviceContext_RSSetViewports(dc(ctx),A(0),A(1)?(const D3D11_VIEWPORT*)rt_ptr(A(1)):NULL);}
static void scissors(X86Ctx*ctx){ID3D11DeviceContext_RSSetScissorRects(dc(ctx),A(0),A(1)?(const D3D11_RECT*)rt_ptr(A(1)):NULL);}
static void blend_state(X86Ctx*ctx){ID3D11DeviceContext_OMSetBlendState(dc(ctx),(ID3D11BlendState*)host_object(A(0)),A(1)?(const FLOAT*)rt_ptr(A(1)):NULL,A(2));}
static void depth_state(X86Ctx*ctx){ID3D11DeviceContext_OMSetDepthStencilState(dc(ctx),(ID3D11DepthStencilState*)host_object(A(0)),A(1));}
static void update_subresource(X86Ctx*ctx){ID3D11DeviceContext_UpdateSubresource(dc(ctx),(ID3D11Resource*)host_object(A(0)),A(1),A(2)?(const D3D11_BOX*)rt_ptr(A(2)):NULL,rt_ptr(A(3)),A(4),A(5));}
static void copy_resource(X86Ctx*ctx){ID3D11DeviceContext_CopyResource(dc(ctx),(ID3D11Resource*)host_object(A(0)),(ID3D11Resource*)host_object(A(1)));}
static void draw(X86Ctx*ctx){__atomic_add_fetch(&oa_draws,1,__ATOMIC_RELAXED);ID3D11DeviceContext_Draw(dc(ctx),A(0),A(1));static unsigned n;if(++n<=3)rt_log("MILESTONE actual original Draw vertices=%u",A(0));}
static void draw_indexed(X86Ctx*ctx){__atomic_add_fetch(&oa_draws,1,__ATOMIC_RELAXED);ID3D11DeviceContext_DrawIndexed(dc(ctx),A(0),A(1),(INT)A(2));static unsigned n;if(++n<=3)rt_log("MILESTONE actual original DrawIndexed indices=%u",A(0));}
static void draw_instanced(X86Ctx*ctx){__atomic_add_fetch(&oa_draws,1,__ATOMIC_RELAXED);ID3D11DeviceContext_DrawInstanced(dc(ctx),A(0),A(1),A(2),A(3));}
static void draw_indexed_instanced(X86Ctx*ctx){__atomic_add_fetch(&oa_draws,1,__ATOMIC_RELAXED);ID3D11DeviceContext_DrawIndexedInstanced(dc(ctx),A(0),A(1),A(2),(INT)A(3),A(4));}

static void copy_subresource_region(X86Ctx*ctx){ID3D11DeviceContext_CopySubresourceRegion(dc(ctx),(ID3D11Resource*)host_object(A(0)),A(1),A(2),A(3),A(4),(ID3D11Resource*)host_object(A(5)),A(6),A(7)?(const D3D11_BOX*)rt_ptr(A(7)):NULL);}
static void resolve_subresource(X86Ctx*ctx){ID3D11DeviceContext_ResolveSubresource(dc(ctx),(ID3D11Resource*)host_object(A(0)),A(1),(ID3D11Resource*)host_object(A(2)),A(3),A(4));}
static void generate_mips(X86Ctx*ctx){ID3D11DeviceContext_GenerateMips(dc(ctx),(ID3D11ShaderResourceView*)host_object(A(0)));}
