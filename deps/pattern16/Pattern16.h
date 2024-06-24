#pragma once

#include "scanners/base.h"

#include <string>

namespace Pattern16 {
	inline void* scan(void* regionStart, size_t regionSize, const std::string& signature, const Impl::Frequencies16& frequencies = Impl::loadFrequencyCache()) {
		auto signature_u8 = Impl::processSignatureString(signature);
		return (void*)Impl::scan(regionStart, regionSize, signature_u8, frequencies);
	}

	inline const void* scan(const void* regionStart, size_t regionSize, const std::string& signature, const Impl::Frequencies16& frequencies = Impl::loadFrequencyCache()) {
		auto signature_u8 = Impl::processSignatureString(signature);
		return Impl::scan(regionStart, regionSize, signature_u8, frequencies);
	}
}
