---
name: Bug Report
about: Create a detailed report about a deficiency in the BitShares Core implementation.

---

**Instructions**
Please include a detailed Title above. Next, please complete the following sections below:
* Bug Description
* Impacts
* Steps To Reproduce
* Expected Behavior
* Screenshots (optional)
* Host Environment (optional)
* Additional Context (optional)

Finally, press the 'Submit new issue' button. The Core Team will evaluate and prioritize your Bug Report for future development. 

**Bug Description**
A clear and concise description of what the bug is.

**Impacts**
Describe which portion(s) of BitShares Core may be impacted by this bug. Please tick at least one box.
- [ ] API (the application programming interface)
- [ ] Build (the build process or something prior to compiled code)
- [ ] CLI (the command line wallet)
- [ ] Deployment (the deployment process after building such as Docker, Travis, etc.)
- [ ] DEX (the Decentralized EXchange, market engine, etc.)
- [ ] P2P (the peer-to-peer network for transaction/block propagation)
- [ ] Performance (system or user efficiency, etc.)
- [ ] Protocol (the blockchain logic, consensus, validation, etc.)
- [ ] Security (the security of system or user data, etc.)
- [ ] UX (the User Experience)
- [ ] Other (please add below)

**Steps To Reproduce**
Steps to reproduce the behavior (example outlined below):
1. Execute API call '...'
2. Using JSON payload '...'
3. Received response '...'
4. See error in screenshot

**Expected Behavior**
A clear and concise description of what you expected to happen.

**Screenshots (optional)**
If applicable, add screenshots to help explain process flow and behavior.

**Host Environment**
Please provide details about the host environment. Much of this information can be found running: `witness_node --version`. 
 - Host OS:             [e.g. Ubuntu 18.04 LTS]
 - Host Physical RAM    [e.g. 4GB]
 - BitShares Version:   [e.g. 2.0.180425]
 - OpenSSL Version:     [e.g. 1.1.0g]
 - Boost Version:       [e.g. 1.65.1]
 
**Additional Context (optional)**
Add any other context about the problem here.

## CORE TEAM TASK LIST
- [ ] Evaluate / Prioritize Bug Report
- [ ] Refine User Stories / Requirements
- [ ] Define Test Cases
- [ ] Design / Develop Solution
- [ ] Perform QA/Testing
- [ ] Update Documentation
