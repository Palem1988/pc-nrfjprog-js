/* Copyright (c) 2015 - 2017, Nordic Semiconductor ASA
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

'use strict';

const nRFjprog = require('../').legacy;

let device;
jasmine.DEFAULT_TIMEOUT_INTERVAL = 100000;

describe('Handles race conditions gracefully', () => {
    beforeAll(done => {
        const callback = (err, connectedDevices) => {
            expect(err).toBeUndefined();
            expect(connectedDevices.length).toBeGreaterThanOrEqual(1);
            device = connectedDevices[0];
            done();
        };

        nRFjprog.getConnectedDevices(callback);
    });

    it('allows multiple, fast, calls in a row', done => {
        let errorCount = 0;
        const getVersionAttempts = 50;
        let callbackCalled = 0;

        const getVersionCallback = (err) => {
            if (err) {
                errorCount++;
            }

            callbackCalled++;

            if (callbackCalled === getVersionAttempts) {
                expect(errorCount).toBe(0);
                done();
            }
        };

        for(let i = 0; i < getVersionAttempts; i++) {
            nRFjprog.getLibraryVersion(getVersionCallback);
        }
    });

    it('returns an error when attempting 5 programs in a row', done => {
        let errorCount = 0;
        const programAttempts = 5;
        let callbackCalled = 0;

        const programCallback = (err) => {
            if (err) {
                errorCount++;
            }

            callbackCalled++;

            if (callbackCalled === programAttempts) {
                expect(errorCount).toBeGreaterThan(0);
                done();
            }
        };

        for(let i = 0; i < programAttempts; i++) {
            nRFjprog.program(device.serialNumber, "./__tests__/hex/connectivity_1.1.0_1m_with_s132_3.0.hex", { }, programCallback);
        }
    });
});
