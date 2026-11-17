// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io' as io;

import 'package:git_repo_tools/git_repo_tools.dart';
import 'package:process_fakes/process_fakes.dart';
import 'package:test/test.dart';

void main() {
  const fakeShaHash = 'fake-sha-hash';

  test('returns non-deleted files which differ from merge-base with main', () async {
    final fixture = Fixture(
      processManager: FakeProcessManager(
        onStart: (FakeCommandLogEntry entry) {
          // Succeed calling "git merge-base --fork-point FETCH_HEAD HEAD".
          if (entry.command.join(' ').startsWith('git merge-base --fork-point')) {
            return FakeProcess(stdout: fakeShaHash);
          }

          // Succeed calling "git fetch upstream main".
          if (entry.command.join(' ') == 'git fetch upstream main') {
            return FakeProcess();
          }

          // Succeed calling "git diff --name-only --diff-filter=ACMRT fake-sha-hash".
          if (entry.command.join(' ') ==
              'git diff --name-only --diff-filter=ACMRT --relative $fakeShaHash') {
            return FakeProcess(stdout: 'file1\nfile2');
          }

          // Otherwise, fail.
          return FakeProcessManager.unhandledStart(entry);
        },
      ),
    );

    try {
      final List<io.File> changedFiles = await fixture.gitRepo.changedFiles;
      expect(changedFiles, hasLength(2));
      expect(changedFiles[0].path, endsWith('file1'));
      expect(changedFiles[1].path, endsWith('file2'));
    } finally {
      fixture.gitRepo.root.deleteSync(recursive: true);
    }
  });

  test('returns non-deleted files which differ from default merge-base', () async {
    final fixture = Fixture(
      processManager: FakeProcessManager(
        onStart: (FakeCommandLogEntry entry) {
          if (entry.command.join(' ').startsWith('git merge-base --fork-point')) {
            return FakeProcess(exitCode: 1);
          }

          if (entry.command.join(' ').startsWith('git merge-base')) {
            return FakeProcess(stdout: fakeShaHash);
          }

          if (entry.command.join(' ') == 'git fetch upstream main') {
            return FakeProcess();
          }

          if (entry.command.join(' ') ==
              'git diff --name-only --diff-filter=ACMRT --relative $fakeShaHash') {
            return FakeProcess(stdout: 'file1\nfile2');
          }

          // Otherwise, fail.
          return FakeProcessManager.unhandledStart(entry);
        },
      ),
    );

    try {
      final List<io.File> changedFiles = await fixture.gitRepo.changedFiles;
      expect(changedFiles, hasLength(2));
      expect(changedFiles[0].path, endsWith('file1'));
      expect(changedFiles[1].path, endsWith('file2'));
    } finally {
      fixture.gitRepo.root.deleteSync(recursive: true);
    }
  });

  test('returns non-deleted files which differ from HEAD', () async {
    final fixture = Fixture(
      processManager: FakeProcessManager(
        onStart: (FakeCommandLogEntry entry) {
          if (entry.command.join(' ') == 'git fetch upstream main') {
            return FakeProcess();
          }

          if (entry.command.join(' ') ==
              'git diff-tree --no-commit-id --name-only --diff-filter=ACMRT --relative -r HEAD') {
            return FakeProcess(stdout: 'file1\nfile2');
          }

          // Otherwise, fail.
          return FakeProcessManager.unhandledStart(entry);
        },
      ),
    );

    try {
      final List<io.File> changedFiles = await fixture.gitRepo.changedFilesAtHead;
      expect(changedFiles, hasLength(2));
      expect(changedFiles[0].path, endsWith('file1'));
      expect(changedFiles[1].path, endsWith('file2'));
    } finally {
      fixture.gitRepo.root.deleteSync(recursive: true);
    }
  });

  test('returns non-deleted files which differ from HEAD when merge-base fails', () async {
    final fixture = Fixture(
      processManager: FakeProcessManager(
        onStart: (FakeCommandLogEntry entry) {
          if (entry.command.join(' ') == 'git fetch upstream main') {
            return FakeProcess();
          }

          if (entry.command.join(' ') ==
              'git diff-tree --no-commit-id --name-only --diff-filter=ACMRT --relative -r HEAD') {
            return FakeProcess(stdout: 'file1\nfile2');
          }

          if (entry.command.join(' ').startsWith('git merge-base --fork-point')) {
            return FakeProcess(exitCode: 1);
          }

          if (entry.command.join(' ').startsWith('git merge-base')) {
            return FakeProcess(stdout: fakeShaHash);
          }

          // Otherwise, fail.
          return FakeProcessManager.unhandledStart(entry);
        },
      ),
    );

    try {
      final List<io.File> changedFiles = await fixture.gitRepo.changedFilesAtHead;
      expect(changedFiles, hasLength(2));
      expect(changedFiles[0].path, endsWith('file1'));
      expect(changedFiles[1].path, endsWith('file2'));
    } finally {
      fixture.gitRepo.root.deleteSync(recursive: true);
    }
  });

  test('verbose output is captured', () async {
    final fixture = Fixture(
      processManager: FakeProcessManager(
        onStart: (FakeCommandLogEntry entry) {
          if (entry.command.join(' ').startsWith('git merge-base --fork-point')) {
            return FakeProcess(exitCode: 1);
          }

          if (entry.command.join(' ').startsWith('git merge-base')) {
            return FakeProcess(stdout: fakeShaHash);
          }

          if (entry.command.join(' ') == 'git fetch upstream main') {
            return FakeProcess();
          }

          if (entry.command.join(' ') ==
              'git diff --name-only --diff-filter=ACMRT --relative $fakeShaHash') {
            return FakeProcess(stdout: 'file1\nfile2');
          }

          // Otherwise, fail.
          return FakeProcessManager.unhandledStart(entry);
        },
      ),
      verbose: true,
    );

    try {
      await fixture.gitRepo.changedFiles;
      expect(
        fixture.logSink.toString(),
        contains('git merge-base --fork-point failed, using default merge-base'),
      );
      expect(fixture.logSink.toString(), contains('git diff output:\nfile1\nfile2'));
    } finally {
      fixture.gitRepo.root.deleteSync(recursive: true);
    }
  });

  /// Fakes a repo where only the fetches in [succeeding] work, and records
  /// every fetch tried, in order.
  FakeProcessManager fetchesOnly(Set<String> succeeding, List<String> tried) {
    return FakeProcessManager(
      onStart: (FakeCommandLogEntry entry) {
        final String command = entry.command.join(' ');
        if (command.startsWith('git fetch ')) {
          tried.add(command);
          return FakeProcess(exitCode: succeeding.contains(command) ? 0 : 1);
        }
        if (command.startsWith('git merge-base --fork-point')) {
          return FakeProcess(stdout: fakeShaHash);
        }
        if (command == 'git diff --name-only --diff-filter=ACMRT --relative $fakeShaHash') {
          return FakeProcess(stdout: 'file1');
        }
        return FakeProcessManager.unhandledStart(entry);
      },
    );
  }

  test('falls back to shorebird/dev when the remote has no main', () async {
    final tried = <String>[];
    final fixture = Fixture(
      processManager: fetchesOnly({'git fetch upstream shorebird/dev'}, tried),
    );
    try {
      expect(await fixture.gitRepo.changedFiles, hasLength(1));
      expect(tried, <String>['git fetch upstream main', 'git fetch upstream shorebird/dev']);
    } finally {
      fixture.gitRepo.root.deleteSync(recursive: true);
    }
  });

  test('tries both branches on upstream before origin', () async {
    final tried = <String>[];
    final fixture = Fixture(processManager: fetchesOnly({'git fetch origin main'}, tried));
    try {
      expect(await fixture.gitRepo.changedFiles, hasLength(1));
      expect(tried, <String>[
        'git fetch upstream main',
        'git fetch upstream shorebird/dev',
        'git fetch origin main',
      ]);
    } finally {
      fixture.gitRepo.root.deleteSync(recursive: true);
    }
  });

  test('uses origin shorebird/dev when only that exists', () async {
    final tried = <String>[];
    final fixture = Fixture(processManager: fetchesOnly({'git fetch origin shorebird/dev'}, tried));
    try {
      expect(await fixture.gitRepo.changedFiles, hasLength(1));
      expect(tried, hasLength(4));
    } finally {
      fixture.gitRepo.root.deleteSync(recursive: true);
    }
  });

  test('fails naming every fetch tried when none works', () async {
    final tried = <String>[];
    final fixture = Fixture(processManager: fetchesOnly({}, tried));
    try {
      await expectLater(
        fixture.gitRepo.changedFiles,
        throwsA(
          predicate(
            (Object e) =>
                e.toString().contains('upstream main') &&
                e.toString().contains('upstream shorebird/dev') &&
                e.toString().contains('origin main') &&
                e.toString().contains('origin shorebird/dev'),
          ),
        ),
      );
    } finally {
      fixture.gitRepo.root.deleteSync(recursive: true);
    }
  });
}

final class Fixture {
  factory Fixture({FakeProcessManager? processManager, bool verbose = false}) {
    final io.Directory root = io.Directory.systemTemp.createTempSync('git_repo_tools.test');
    final logSink = StringBuffer();
    processManager ??= FakeProcessManager();
    return Fixture._(
      gitRepo: GitRepo.fromRoot(
        root,
        logSink: logSink,
        processManager: processManager,
        verbose: verbose,
      ),
      logSink: logSink,
    );
  }

  const Fixture._({required this.gitRepo, required this.logSink});

  final GitRepo gitRepo;
  final StringBuffer logSink;
}
