#include "stdafx.h"
#include "bgfxWallMarkArray.h"

#include "bgfxUIShader.h"

void bgfxWallMarkArray::Copy(IWallMarkArray &_in)
{
    m_marks = ((bgfxWallMarkArray&)_in).m_marks;
}

void bgfxWallMarkArray::AppendMark(LPCSTR s_textures)
{
    if (!s_textures || !s_textures[0])
        return;
    m_marks.push_back(xr_string(s_textures));
}

void bgfxWallMarkArray::clear()
{
    m_marks.clear();
}

bool bgfxWallMarkArray::empty()
{
    return m_marks.empty();
}

wm_shader bgfxWallMarkArray::GenerateWallmark()
{
    wm_shader res;
    if (!m_marks.empty())
    {
        const char* name = m_marks[::Random.randI(0, (int)m_marks.size())].c_str();
        res->create("effects\\wallmark", name);
    }
    return res;
}
