# Academic Citations

This document provides formal academic citations for work that influenced or is incorporated into XSC OS.

## Primary References

### FlexSC: Exception-Less System Calls

The core concept of ring-based system calls in XSC is based on the FlexSC research:

```bibtex
@inproceedings{soares2010flexsc,
  title={FlexSC: Flexible System Call Scheduling with Exception-Less System Calls},
  author={Soares, Livio and Stumm, Michael},
  booktitle={Proceedings of the 9th USENIX Conference on Operating Systems Design and Implementation},
  pages={33--46},
  year={2010},
  organization={USENIX Association},
  address={Vancouver, BC, Canada},
  url={https://www.usenix.org/legacy/event/osdi10/tech/full_papers/Soares.pdf}
}
```

**Key Contributions:**
- Demonstrated exception-less system calls using ring buffers
- Showed significant performance improvements from reduced mode transitions
- Introduced flexible scheduling of system call execution

**How XSC Extends FlexSC:**
- Production implementation for modern Linux (6.1+)
- Hardware control-flow integrity enforcement (Intel CET, ARM PAC)
- Complete distribution infrastructure
- Support for both x86-64 and ARM64 architectures
- Two-tier security model (base and hardened variants)

## Related Work

### Control-Flow Integrity

```bibtex
@inproceedings{abadi2005cfi,
  title={Control-Flow Integrity Principles, Implementations, and Applications},
  author={Abadi, Mart{\'\i}n and Budiu, Mihai and Erlingsson, {\'U}lfar and Ligatti, Jay},
  booktitle={Proceedings of the 12th ACM Conference on Computer and Communications Security},
  pages={340--353},
  year={2005}
}
```

### Intel CET (Control-flow Enforcement Technology)

Intel Corporation. *Control-flow Enforcement Technology Specification*.
Available at: https://www.intel.com/content/www/us/en/developer/articles/technical/technical-look-control-flow-enforcement-technology.html

### ARM Pointer Authentication

ARM Limited. *ARM Architecture Reference Manual Supplement - The ARMv8.3 Pointer Authentication Extension*.
Available at: https://developer.arm.com/documentation/

## Historical Context

Early concepts for hardware-enforced return address protection were discussed in security communities over a decade before hardware implementations became available. Shadow stack mechanisms, now implemented in Intel CET and similar technologies, address fundamental vulnerabilities in return-oriented programming (ROP) attacks.

## License

This document is provided for academic reference purposes. Citations should follow standard academic practices for the relevant field (computer science, systems, security).

---

*Last Updated: 2025-10-12*
