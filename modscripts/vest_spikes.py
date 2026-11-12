"""Marauder: eine silberne Schulterplatte links (x<0). vest_spikes.py PIE"""
import sys, os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import plates
W = (241 / 256, 246 / 256)
print("Platte:", plates.apply(sys.argv[1], [(-1, W, W)]))
