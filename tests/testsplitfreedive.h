// SPDX-License-Identifier: GPL-2.0
#ifndef TESTSPLITFREEDIVE_H
#define TESTSPLITFREEDIVE_H

#include "testbase.h"

class TestSplitFreedive : public TestBase {
	Q_OBJECT
private slots:
	void initTestCase();
	void cleanup();

	void testSplitFreediveSessionSynthetic();
	void testSplitFreediveSessionRealData();
	void testSplitFreediveSessionCommandNewSite();
	void testSplitFreediveSessionCommandExistingSite();
};

#endif // TESTSPLITFREEDIVE_H
