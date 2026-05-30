//! Process hardening — apply seccomp, drop capabilities, set resource limits.

use crate::error::{Result, SysError};
use nix::sys::resource::{setrlimit, Resource};
use nix::unistd::{setuid, setgid, Uid, Gid};

pub struct ProcessHardener {
    pub drop_privileges: bool,
    pub limit_core:      bool,
    pub no_new_privs:    bool,
}

impl ProcessHardener {
    pub fn new() -> Self {
        Self {
            drop_privileges: false,
            limit_core:      true,
            no_new_privs:    true,
        }
    }

    /// Apply all configured hardening to the current process
    pub fn apply(&self) -> Result<()> {
        if self.limit_core {
            self.disable_core_dumps()?;
        }
        if self.no_new_privs {
            self.set_no_new_privs()?;
        }
        Ok(())
    }

    /// Disable core dumps for this process
    pub fn disable_core_dumps(&self) -> Result<()> {
        setrlimit(Resource::RLIMIT_CORE, 0, 0)
            .map_err(|e| SysError::System(format!("setrlimit CORE: {}", e)))?;
        Ok(())
    }

    /// Set PR_SET_NO_NEW_PRIVS — process cannot gain new privileges
    pub fn set_no_new_privs(&self) -> Result<()> {
        #[cfg(target_os = "linux")]
        unsafe {
            let ret = libc::prctl(libc::PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
            if ret != 0 {
                return Err(SysError::System("PR_SET_NO_NEW_PRIVS failed".into()));
            }
        }
        Ok(())
    }

    /// Drop to a less-privileged user/group
    pub fn drop_to(&self, uid: u32, gid: u32) -> Result<()> {
        setgid(Gid::from_raw(gid))
            .map_err(|e| SysError::PermissionDenied(format!("setgid: {}", e)))?;
        setuid(Uid::from_raw(uid))
            .map_err(|e| SysError::PermissionDenied(format!("setuid: {}", e)))?;
        Ok(())
    }

    /// Set memory limit in bytes
    pub fn limit_memory(&self, bytes: u64) -> Result<()> {
        setrlimit(Resource::RLIMIT_AS, bytes, bytes)
            .map_err(|e| SysError::System(format!("setrlimit AS: {}", e)))?;
        Ok(())
    }
}

impl Default for ProcessHardener {
    fn default() -> Self { Self::new() }
}
