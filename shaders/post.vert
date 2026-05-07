#version 450

// -----------------------------------------------------------------------------
// Post-process Vertex Shader
// Generates a full-screen triangle from gl_VertexID - no VBO required.
// Three vertices cover the entire screen with UVs in [0,1] over the visible
// portion. Caller binds any VAO and issues glDrawArrays(GL_TRIANGLES, 0, 3).
// -----------------------------------------------------------------------------

layout(location = 0) out vec2 vUV;

void main()
{
    // Vertex 0 -> NDC (-1, -1), UV (0, 0)
    // Vertex 1 -> NDC ( 3, -1), UV (2, 0)
    // Vertex 2 -> NDC (-1,  3), UV (0, 2)
    // The triangle's bounds get clipped to the screen, leaving UV in [0,1] over
    // the visible region.
    vec2 pos = vec2((gl_VertexID == 1) ? 3.0 : -1.0, (gl_VertexID == 2) ? 3.0 : -1.0);
    vUV = (pos + 1.0) * 0.5;
    gl_Position = vec4(pos, 0.0, 1.0);
}
