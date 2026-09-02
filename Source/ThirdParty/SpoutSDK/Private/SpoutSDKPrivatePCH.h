// Copyright (c) 2026 SpoutUE contributors. MIT licensed.
//
// Force-included into every translation unit of the SpoutSDK module.
//
// The vendored Spout sources are plain Win32 C++ compiled here under Unreal's
// /W4 settings. Rather than patching upstream (which would make future SDK
// updates painful), we relax the specific warnings this code trips so the
// vendored files stay byte-identical to the upstream release.

#pragma once

#if defined(_MSC_VER)
	#pragma warning(disable: 4100) // unreferenced formal parameter
	#pragma warning(disable: 4127) // conditional expression is constant
	#pragma warning(disable: 4189) // local variable initialized but not referenced
	#pragma warning(disable: 4245) // signed/unsigned mismatch in conversion
	#pragma warning(disable: 4267) // conversion from size_t, possible loss of data
	#pragma warning(disable: 4302) // truncation type cast
	#pragma warning(disable: 4311) // pointer truncation
	#pragma warning(disable: 4312) // conversion to greater size
	#pragma warning(disable: 4324) // structure was padded due to alignment specifier
	#pragma warning(disable: 4365) // signed/unsigned mismatch
	#pragma warning(disable: 4388) // signed/unsigned mismatch in comparison
	#pragma warning(disable: 4456) // declaration hides previous local declaration
	#pragma warning(disable: 4457) // declaration hides function parameter
	#pragma warning(disable: 4458) // declaration hides class member
	#pragma warning(disable: 4459) // declaration hides global declaration
	#pragma warning(disable: 4505) // unreferenced local function has been removed
	#pragma warning(disable: 4668) // symbol not defined as a preprocessor macro
	#pragma warning(disable: 4996) // deprecated CRT functions
	#pragma warning(disable: 5219) // implicit conversion, possible loss of data
#endif
