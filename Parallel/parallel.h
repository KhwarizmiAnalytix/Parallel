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
 */

/**
 * @file parallel.h
 * @brief Single umbrella include for the Parallel library's public API.
 *
 * `#include "Parallel/parallel.h"` pulls in everything a client needs:
 *
 * - parallel_tools   — the main entry point: parallel_for() / parallel_reduce()
 * - multi_threader          — direct thread control (advanced)
 * - threaded_callback_queue — async tasks with dependencies (advanced)
 * - threaded_task_queue     — worker-function stream processing (advanced)
 *
 * See the "Public API" section of the repository README for usage examples of each.
 *
 * Not to be confused with Parallel/tools/parallel.h, which is an internal header (it declares
 * the backend_type enum used by the backend-selection machinery) — this file is the one meant
 * for `#include`.
 */

#pragma once

#include "Parallel/tools/multi_threader.h"
#include "Parallel/tools/parallel_tools.h"
#include "Parallel/tools/threaded_callback_queue.h"
#include "Parallel/tools/threaded_task_queue.h"
