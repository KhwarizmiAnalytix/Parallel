/*
 * XSigma: High-Performance Computational Library
 *
 * SPDX-License-Identifier: GPL-3.0-or-later OR Commercial
 *
 * This file is part of XSigma and is licensed under a dual-license model:
 *
 *   - Open-source License (GPLv3):
 *       Free for personal, academic, and research use under the terms of
 *       the GNU General Public License v3.0 or later.
 *
 *   - Commercial License:
 *       A commercial license is required for proprietary, closed-source,
 *       or SaaS usage. Contact us to obtain a commercial agreement.
 *
 * Contact: licensing@xsigma.co.uk
 * Website: https://www.xsigma.co.uk
 *
 * Portions of this code are based on VTK (Visualization Toolkit):

 *   Licensed under BSD-3-Clause
 */

#include "parallel_tools.h"

//------------------------------------------------------------------------------
void parallel_tools::initialize(int num_threads)
{
    auto& SMPToolsAPI = parallel::detail::parallel_impl::parallel_tools_api::instance();
    SMPToolsAPI.initialize(num_threads);
}

//------------------------------------------------------------------------------
int parallel_tools::estimated_number_of_threads()
{
    auto& SMPToolsAPI = parallel::detail::parallel_impl::parallel_tools_api::instance();
    return SMPToolsAPI.estimated_number_of_threads();
}

//------------------------------------------------------------------------------
int parallel_tools::estimated_default_number_of_threads()
{
    auto& SMPToolsAPI = parallel::detail::parallel_impl::parallel_tools_api::instance();
    return SMPToolsAPI.estimated_default_number_of_threads();
}

//------------------------------------------------------------------------------
void parallel_tools::set_nested_parallelism(bool is_nested)
{
    auto& SMPToolsAPI = parallel::detail::parallel_impl::parallel_tools_api::instance();
    SMPToolsAPI.set_nested_parallelism(is_nested);
}

//------------------------------------------------------------------------------
bool parallel_tools::nested_parallelism()
{
    auto& SMPToolsAPI = parallel::detail::parallel_impl::parallel_tools_api::instance();
    return SMPToolsAPI.nested_parallelism();
}

//------------------------------------------------------------------------------
bool parallel_tools::is_parallel_scope()
{
    auto& SMPToolsAPI = parallel::detail::parallel_impl::parallel_tools_api::instance();
    return SMPToolsAPI.is_parallel_scope();
}

//------------------------------------------------------------------------------
bool parallel_tools::single_thread()
{
    auto& SMPToolsAPI = parallel::detail::parallel_impl::parallel_tools_api::instance();
    return SMPToolsAPI.single_thread();
}
