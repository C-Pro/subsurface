// SPDX-License-Identifier: GPL-2.0
#include "testsplitfreedive.h"
#include "core/dive.h"
#include "core/divelist.h"
#include "core/divelog.h"
#include "core/divesite.h"
#include "core/trip.h"
#include "core/pref.h"
#include "core/file.h"
#include "core/sample.h"
#include "core/profile.h"
#include "commands/command.h"
#include "commands/command_base.h"

void TestSplitFreedive::initTestCase()
{
	TestBase::initTestCase();
	prefs = default_prefs;
	Command::init();
}

void TestSplitFreedive::cleanup()
{
	clear_dive_file_data();
	Command::getUndoStack()->clear();
}

static struct sample make_sample(int sec, int depth_mm)
{
	struct sample s{};
	s.time = duration_t{ .seconds = sec };
	s.depth = depth_t::from_base(depth_mm);
	return s;
}

void TestSplitFreedive::testSplitFreediveSessionSynthetic()
{
	// 1. Non-freedive mode (OC) must return empty splits
	{
		dive d;
		d.when = 1600000000;
		struct divecomputer dc{};
		dc.divemode = OC;
		dc.samples.push_back(make_sample(0, 0));
		dc.samples.push_back(make_sample(10, 5000));
		dc.samples.push_back(make_sample(20, 0));
		dc.samples.push_back(make_sample(35, 0)); // 15s surface
		dc.samples.push_back(make_sample(45, 5000));
		dc.samples.push_back(make_sample(55, 0));
		d.dcs[0] = std::move(dc);

		auto splits = divelog.dives.split_freedive_session(d);
		QVERIFY(splits.empty());
	}

	// 2. Insufficient samples (< 3)
	{
		dive d;
		d.when = 1600000000;
		struct divecomputer dc{};
		dc.divemode = FREEDIVE;
		dc.samples.push_back(make_sample(0, 0));
		dc.samples.push_back(make_sample(10, 5000));
		d.dcs[0] = std::move(dc);

		auto splits = divelog.dives.split_freedive_session(d);
		QVERIFY(splits.empty());
	}

	// 3. Surface interval shorter than threshold (< 10s)
	{
		dive d;
		d.when = 1600000000;
		struct divecomputer dc{};
		dc.divemode = FREEDIVE;
		dc.samples.push_back(make_sample(0, 0));
		dc.samples.push_back(make_sample(10, 5000));
		dc.samples.push_back(make_sample(20, 0));
		dc.samples.push_back(make_sample(27, 0)); // 7s surface interval
		dc.samples.push_back(make_sample(35, 5000));
		dc.samples.push_back(make_sample(45, 0));
		d.dcs[0] = std::move(dc);

		auto splits = divelog.dives.split_freedive_session(d);
		QVERIFY(splits.empty());
	}

	// 4. Two valid freedives separated by 12s surface interval
	{
		dive d;
		d.number = 7;
		d.when = 1600000000;
		struct divecomputer dc{};
		dc.divemode = FREEDIVE;
		// Sub-dive 1: 0s to 30s
		dc.samples.push_back(make_sample(0, 0));
		dc.samples.push_back(make_sample(10, 3000));
		dc.samples.push_back(make_sample(20, 5000));
		dc.samples.push_back(make_sample(30, 0));
		// Surface interval: 30s to 42s (12s >= 10s)
		dc.samples.push_back(make_sample(36, 0));
		dc.samples.push_back(make_sample(42, 0));
		// Sub-dive 2: 42s to 70s
		dc.samples.push_back(make_sample(50, 4000));
		dc.samples.push_back(make_sample(60, 8000));
		dc.samples.push_back(make_sample(70, 0));
		// Trailing surface: 70s to 85s
		dc.samples.push_back(make_sample(85, 0));
		d.dcs[0] = std::move(dc);

		auto splits = divelog.dives.split_freedive_session(d);
		QCOMPARE(splits.size(), static_cast<size_t>(2));

		// Sub-dive 1 checks
		QCOMPARE(splits[0]->when, static_cast<timestamp_t>(1600000000));
		QCOMPARE(splits[0]->duration.seconds, 30);
		QCOMPARE(splits[0]->maxdepth.mm, 5000);
		QCOMPARE(splits[0]->dcs[0].samples.size(), static_cast<size_t>(4));
		QCOMPARE(splits[0]->dcs[0].samples[0].time.seconds, 0);
		QCOMPARE(splits[0]->dcs[0].samples.back().time.seconds, 30);

		// Sub-dive 2 checks (rebased from 42s)
		QCOMPARE(splits[1]->when, static_cast<timestamp_t>(1600000042));
		QCOMPARE(splits[1]->duration.seconds, 28); // 70 - 42
		QCOMPARE(splits[1]->maxdepth.mm, 8000);
		QCOMPARE(splits[1]->dcs[0].samples.size(), static_cast<size_t>(4));
		QCOMPARE(splits[1]->dcs[0].samples[0].time.seconds, 0);
		QCOMPARE(splits[1]->dcs[0].samples.back().time.seconds, 28);
	}
}

void TestSplitFreedive::testSplitFreediveSessionRealData()
{
	struct divelog log;
	QString testFile = QStringLiteral(SUBSURFACE_TEST_DATA "/cpro-subsurface-split.xml");
	if (!QFile::exists(testFile))
		QSKIP("cpro-subsurface-split.xml not found");

	int res = parse_file(qPrintable(testFile), &log);
	QCOMPARE(res, 0);

	struct dive *dive26 = nullptr;
	for (const auto &d : log.dives) {
		if (d->number == 26 && !d->dcs.empty() && d->dcs[0].divemode == FREEDIVE && d->dcs[0].samples.size() > 100) {
			dive26 = d.get();
			break;
		}
	}
	QVERIFY(dive26 != nullptr);

	auto splits = log.dives.split_freedive_session(*dive26);
	// In cpro-subsurface-split.xml, dive 26 contains 10 separate freedives
	QCOMPARE(splits.size(), static_cast<size_t>(10));

	for (size_t i = 0; i < splits.size(); ++i) {
		QVERIFY(!splits[i]->dcs.empty());
		QCOMPARE(splits[i]->dcs[0].divemode, FREEDIVE);
		QVERIFY(splits[i]->maxdepth.mm > 1000);
		QVERIFY(splits[i]->duration.seconds > 0);
		if (i > 0) {
			QVERIFY(splits[i]->when > splits[i - 1]->when);
			QVERIFY(splits[i]->when >= splits[i - 1]->when + splits[i - 1]->duration.seconds);
		}
	}

	// Verify that corrupted events with timestamps past the end of the dive were purged during fixup
	for (const auto &ev : dive26->dcs[0].events) {
		QVERIFY(ev.time.seconds <= dive26->dcs[0].samples.back().time.seconds + 300);
	}

	// Verify that create_plot_info_new produces a sane maxtime (under 1 hour, not 1 billion)
	plot_info pi = create_plot_info_new(dive26, &dive26->dcs[0], nullptr);
	QVERIFY(pi.maxtime <= 1800);
	QCOMPARE(get_maxtime(pi), 1800); // 30 min default window
}

void TestSplitFreedive::testSplitFreediveSessionCommandNewSite()
{
	clear_dive_file_data();
	Command::getUndoStack()->clear();

	auto srcDive = std::make_unique<dive>();
	srcDive->number = 42;
	srcDive->when = 1600000000;
	struct divecomputer dc{};
	dc.divemode = FREEDIVE;
	dc.samples.push_back(make_sample(0, 0));
	dc.samples.push_back(make_sample(10, 5000));
	dc.samples.push_back(make_sample(20, 0));
	dc.samples.push_back(make_sample(35, 0)); // 15s surface
	dc.samples.push_back(make_sample(45, 6000));
	dc.samples.push_back(make_sample(55, 0));
	srcDive->dcs[0] = std::move(dc);

	dive *d = divelog.dives.register_dive(std::move(srcDive));
	QCOMPARE(divelog.dives.size(), static_cast<size_t>(1));
	QCOMPARE(divelog.trips.size(), static_cast<size_t>(0));
	QCOMPARE(divelog.sites.size(), static_cast<size_t>(0));

	auto splits = divelog.dives.split_freedive_session(*d);
	QCOMPARE(splits.size(), static_cast<size_t>(2));

	location_t loc = create_location(12.345, 109.876);
	auto newSite = std::make_unique<dive_site>("Paradise Reef", loc);

	Command::splitFreediveSession(d, std::move(splits), nullptr, std::move(newSite));

	// Verification after redo (initial execution)
	QCOMPARE(divelog.dives.size(), static_cast<size_t>(2));
	QCOMPARE(divelog.trips.size(), static_cast<size_t>(1));
	QCOMPARE(divelog.trips[0]->location, std::string("Paradise Reef"));
	QCOMPARE(divelog.sites.size(), static_cast<size_t>(1));
	QCOMPARE(divelog.sites[0]->name, std::string("Paradise Reef"));
	QCOMPARE(divelog.dives[0]->number, 42);
	QCOMPARE(divelog.dives[1]->number, 0);
	QCOMPARE(divelog.dives[0]->dive_site, divelog.sites[0].get());
	QCOMPARE(divelog.dives[1]->dive_site, divelog.sites[0].get());
	QCOMPARE(divelog.dives[0]->divetrip, divelog.trips[0].get());
	QCOMPARE(divelog.dives[1]->divetrip, divelog.trips[0].get());

	// Test Undo
	Command::getUndoStack()->undo();
	QCOMPARE(divelog.dives.size(), static_cast<size_t>(1));
	QCOMPARE(divelog.dives[0]->number, 42);
	QCOMPARE(divelog.trips.size(), static_cast<size_t>(0));
	QCOMPARE(divelog.sites.size(), static_cast<size_t>(0));

	// Test Redo
	Command::getUndoStack()->redo();
	QCOMPARE(divelog.dives.size(), static_cast<size_t>(2));
	QCOMPARE(divelog.trips.size(), static_cast<size_t>(1));
	QCOMPARE(divelog.sites.size(), static_cast<size_t>(1));
	QCOMPARE(divelog.trips[0]->location, std::string("Paradise Reef"));
	QCOMPARE(divelog.dives[0]->dive_site, divelog.sites[0].get());
	QCOMPARE(divelog.dives[1]->dive_site, divelog.sites[0].get());
}

void TestSplitFreedive::testSplitFreediveSessionCommandExistingSite()
{
	clear_dive_file_data();
	Command::getUndoStack()->clear();

	location_t loc = create_location(12.345, 109.876);
	dive_site *existingSite = divelog.sites.create("Existing Coral Bay", loc);
	QVERIFY(existingSite != nullptr);

	auto srcDive = std::make_unique<dive>();
	srcDive->number = 100;
	srcDive->when = 1600000000;
	struct divecomputer dc{};
	dc.divemode = FREEDIVE;
	dc.samples.push_back(make_sample(0, 0));
	dc.samples.push_back(make_sample(10, 5000));
	dc.samples.push_back(make_sample(20, 0));
	dc.samples.push_back(make_sample(35, 0));
	dc.samples.push_back(make_sample(45, 6000));
	dc.samples.push_back(make_sample(55, 0));
	srcDive->dcs[0] = std::move(dc);

	dive *d = divelog.dives.register_dive(std::move(srcDive));

	auto splits = divelog.dives.split_freedive_session(*d);
	QCOMPARE(splits.size(), static_cast<size_t>(2));

	Command::splitFreediveSession(d, std::move(splits), existingSite, nullptr);

	QCOMPARE(divelog.dives.size(), static_cast<size_t>(2));
	QCOMPARE(divelog.trips.size(), static_cast<size_t>(1));
	QCOMPARE(divelog.trips[0]->location, std::string("Existing Coral Bay"));
	QCOMPARE(divelog.sites.size(), static_cast<size_t>(1));
	QCOMPARE(divelog.dives[0]->dive_site, existingSite);
	QCOMPARE(divelog.dives[1]->dive_site, existingSite);

	// Test Undo: existing site must NOT be deleted!
	Command::getUndoStack()->undo();
	QCOMPARE(divelog.dives.size(), static_cast<size_t>(1));
	QCOMPARE(divelog.dives[0]->number, 100);
	QCOMPARE(divelog.trips.size(), static_cast<size_t>(0));
	QCOMPARE(divelog.sites.size(), static_cast<size_t>(1));
	QCOMPARE(divelog.sites[0].get(), existingSite);
}

QTEST_GUILESS_MAIN(TestSplitFreedive)
