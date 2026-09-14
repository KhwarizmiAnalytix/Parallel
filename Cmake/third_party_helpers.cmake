# =============================================================================
# Parallel — third-party target helpers
# =============================================================================
# Slim copies of the XSigma ThirdParty/CMakeLists.txt helpers used by this
# repo's build: namespaced alias creation and IDE folder properties. When the
# repo is embedded in XSigma (add_subdirectory), XSigma's own definitions of
# these functions already exist — the include_guard + identical bodies keep
# that case harmless.
# =============================================================================

include_guard(GLOBAL)

# -----------------------------------------------------------------------------
# _create_third_party_interface_targets: Create namespaced aliases
# -----------------------------------------------------------------------------
# Creates namespaced aliases for third-party library targets (e.g.
# "Benchmark::benchmark=benchmark::benchmark|benchmark" — each source target is
# tried in order; IMPORTED targets get an INTERFACE wrapper since ALIAS cannot
# alias IMPORTED).
function(_create_third_party_interface_targets)
  set(xsigma_targets ${ARGN})

  if(NOT xsigma_targets)
    return()
  endif()

  foreach(target_spec ${xsigma_targets})
    # Parse the specification: "Xxx::name=target1|target2|..."
    string(REPLACE "=" ";" spec_parts "${target_spec}")
    list(LENGTH spec_parts spec_parts_len)

    if(NOT spec_parts_len EQUAL 2)
      message(WARNING "Invalid namespaced target specification: ${target_spec}")
      continue()
    endif()

    list(GET spec_parts 0 xsigma_name)
    list(GET spec_parts 1 source_targets)

    # Check if namespaced target already exists
    if(TARGET ${xsigma_name})
      message(STATUS "Target ${xsigma_name} already exists, skipping")
      continue()
    endif()

    # Split source targets by pipe (|) to get alternatives
    string(REPLACE "|" ";" source_target_list "${source_targets}")

    set(target_created FALSE)
    foreach(source_target ${source_target_list})
      if(TARGET ${source_target})
        # Check if source target is IMPORTED (from find_package)
        get_target_property(is_imported ${source_target} IMPORTED)

        if(is_imported)
          # For IMPORTED targets, create an INTERFACE library that links to it
          # (ALIAS targets cannot alias IMPORTED targets)
          add_library(${xsigma_name} INTERFACE IMPORTED GLOBAL)
          target_link_libraries(${xsigma_name} INTERFACE ${source_target})
          message(
            STATUS
              "Created ${xsigma_name} -> ${source_target} (INTERFACE wrapper for IMPORTED target)"
          )
        else()
          # Check if source target is an alias and resolve it
          get_target_property(aliased_target ${source_target} ALIASED_TARGET)

          if(aliased_target)
            # Source is an alias, create alias to the actual target
            add_library(${xsigma_name} ALIAS ${aliased_target})
            message(
              STATUS "Created ${xsigma_name} -> ${source_target} (resolved to ${aliased_target})"
            )
          else()
            # Source is a real target, create alias directly
            add_library(${xsigma_name} ALIAS ${source_target})
            message(STATUS "Created ${xsigma_name} -> ${source_target}")
          endif()
        endif()

        set(target_created TRUE)
        break()
      endif()
    endforeach()

    if(NOT target_created)
      message(
        WARNING
          "Could not create alias ${xsigma_name}: none of the source targets exist (${source_targets})"
      )
    endif()
  endforeach()
endfunction()

# -----------------------------------------------------------------------------
# _set_third_party_folder_properties: Set FOLDER property for all targets
# -----------------------------------------------------------------------------
# Recursively collects all targets created under a binary directory and groups
# them under "ThirdParty/<name>" in IDEs.
function(_set_third_party_folder_properties name binary_dir)
  function(_collect_targets_recursive dir collected_targets_var)
    get_property(dir_targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)

    if(dir_targets)
      list(APPEND local_targets ${dir_targets})
    endif()

    get_property(subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)

    foreach(subdir ${subdirs})
      _collect_targets_recursive("${subdir}" local_targets)
    endforeach()

    set(${collected_targets_var} "${local_targets}" PARENT_SCOPE)
  endfunction()

  _collect_targets_recursive("${binary_dir}" all_targets)

  if(all_targets)
    foreach(target ${all_targets})
      if(TARGET ${target})
        set_target_properties(${target} PROPERTIES FOLDER "ThirdParty/${name}")
      endif()
    endforeach()
  endif()
endfunction()
