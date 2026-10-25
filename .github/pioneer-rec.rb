# This file is the template for the Homebrew cask.
# On release, the workflow fills in the version and SHA256 checksums.
cask "pioneer-rec" do
  version "VERSION_PLACEHOLDER"

  on_macos do
    on_arm do
      sha256 "DARWIN_ARM64_SHA_PLACEHOLDER"
      url "https://github.com/rm-hull/pioneer-rec/releases/download/v#{version}/pioneer-rec-v#{version}-darwin_arm64.tar.gz"
    end
    on_intel do
      sha256 "DARWIN_AMD64_SHA_PLACEHOLDER"
      url "https://github.com/rm-hull/pioneer-rec/releases/download/v#{version}/pioneer-rec-v#{version}-darwin_amd64.tar.gz"
    end
  end
  on_linux do
    on_arm do
      sha256 "LINUX_ARM64_SHA_PLACEHOLDER"
      url "https://github.com/rm-hull/pioneer-rec/releases/download/v#{version}/pioneer-rec-v#{version}-linux_arm64.tar.gz"
    end
    on_intel do
      sha256 "LINUX_AMD64_SHA_PLACEHOLDER"
      url "https://github.com/rm-hull/pioneer-rec/releases/download/v#{version}/pioneer-rec-v#{version}-linux_amd64.tar.gz"
    end
  end

  name "pioneer-rec"
  desc "Recover video recordings from failed Pioneer DVR and Sony HXD recorder hard drives."
  homepage "https://github.com/rm-hull/pioneer-rec"

  livecheck do
    skip "Auto-generated on release."
  end

  binary "pioneer_rec"
  binary "pioneer_rec_520"
  binary "pioneer_rec_545"
  binary "pioneer_rec_550"

  postflight_steps do
    on_macos do
      run "/usr/bin/xattr", args: ["-dr", "com.apple.quarantine", "{{staged_path}}/pioneer_rec"]
      run "/usr/bin/xattr", args: ["-dr", "com.apple.quarantine", "{{staged_path}}/pioneer_rec_520"]
      run "/usr/bin/xattr", args: ["-dr", "com.apple.quarantine", "{{staged_path}}/pioneer_rec_545"]
      run "/usr/bin/xattr", args: ["-dr", "com.apple.quarantine", "{{staged_path}}/pioneer_rec_550"]
    end
  end

  # No zap stanza required
end
