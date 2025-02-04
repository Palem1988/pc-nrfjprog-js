/* Copyright (c) 2015 - 2019, Nordic Semiconductor ASA
 *
 * All rights reserved.
 *
 * Use in source and binary forms, redistribution in binary form only, with
 * or without modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions in binary form, except as embedded into a Nordic
 *    Semiconductor ASA integrated circuit in a product or a software update for
 *    such product, must reproduce the above copyright notice, this list of
 *    conditions and the following disclaimer in the documentation and/or other
 *    materials provided with the distribution.
 *
 * 2. Neither the name of Nordic Semiconductor ASA nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * 3. This software, with or without modification, must only be used with a Nordic
 *    Semiconductor ASA integrated circuit.
 *
 * 4. Any software provided in binary form under this license must not be reverse
 *    engineered, decompiled, modified and/or disassembled.
 *
 * THIS SOFTWARE IS PROVIDED BY NORDIC SEMICONDUCTOR ASA "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, NONINFRINGEMENT, AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL NORDIC SEMICONDUCTOR ASA OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

const path = require('path');
const nRFjprog = require('bindings')('pc-nrfjprog-js');

const instance = new nRFjprog.nRFjprog();

// When this module is bundled with nRFConnect application the location of
// nRFjprog libraries depend on the target platform, therefore we give the
// possibility to nRFConnect to set the expected location via environment
// variable. If not set, the path is expected to be under this module:
const envLibPath = process.env.NRFJPROG_LIBRARY_PATH;
const moduleLibPath = path.join(__dirname, 'nrfjprog');

Object.keys(nRFjprog).forEach(key => {
    if (key === 'setLibrarySearchPath') {
        nRFjprog.setLibrarySearchPath(envLibPath || moduleLibPath);
        instance.getLibraryVersion(err => {
            if (err) {
                // obsolete fallback, only works in some cases
                nRFjprog.setLibrarySearchPath(process.cwd());
            }
        });
    } else if (key !== 'nRFjprog') {
        instance[key] = nRFjprog[key];
    }
});

module.exports = instance;
module.exports.RTT = new nRFjprog.RTT();
