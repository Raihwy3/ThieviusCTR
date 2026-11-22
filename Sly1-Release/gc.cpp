#include "gc.h"

void CGc::Startup()
{
    m_cpsidebagRoot = 0;
    m_cpframeRoot = 0;
}

void CGc::AddRootFrame(CFrame* pframe)
{
    if (pframe == nullptr)
        return;

    for (int i = 0; i < m_cpframeRoot; i++)
    {
        if (m_apframeRoot[i] == pframe)
            return;
    }

    if (m_cpframeRoot < 256)
    {
        m_apframeRoot[m_cpframeRoot] = pframe;
        m_cpframeRoot++;
    }
}

void CGc::AddRootSidebag(CSidebag* pSidebag)
{
	if (pSidebag == nullptr)
		return;

	for (int i = 0; i < m_cpsidebagRoot; ++i)
	{
		if (m_apsidebagRoot[i] == pSidebag)
			return;
	}

	if (m_cpsidebagRoot < 128)
		m_apsidebagRoot[m_cpsidebagRoot++] = pSidebag;
}

void CGc::ShutDown()
{
    m_cpframeRoot = 0;
    m_cpsidebagRoot = 0;
    m_cpframeStack = 0;
    m_cppairStack = 0;
    m_cpprocStack = 0;

    for (auto& p : m_apframeRoot)
        p = nullptr;

    for (auto& p : m_apsidebagRoot)
        p = nullptr;

    for (auto& p : m_apframeStack)
        p = nullptr;

    for (auto& p : m_appairStack)
        p = nullptr;

    for (auto& p : m_approcStack)
        p = nullptr;
}

CGc g_gc;
